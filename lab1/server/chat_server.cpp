#include "chat_server.h"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace chatproto;

/**
 * 启动服务器，监听指定端口
 * @param port 监听端口
 */
bool ChatServer::start(uint16_t port) {
    if (running_.load()) return true;  // 如果已经运行则直接返回 true
    // 创建监听套接字
    SOCKET& listenSockRef = listenSock_;
    // af: IPv4,type:SOCK_STREAM, protocol:TCP 流式套接字
    listenSockRef = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSockRef == INVALID_SOCKET) return false;

    // 允许地址重用
    u_long yes = 1;
    // 设置套接字选项 允许地址重用 SO_REUSEADDR
    setsockopt(listenSockRef, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes,
               sizeof(yes));

    // 绑定地址和端口
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 绑定到所有接口
    addr.sin_port = htons(port);

    // 将套接字绑定到指定的 IP 地址和端口
    if (bind(listenSockRef, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenSockRef);
        listenSockRef = INVALID_SOCKET;
        return false;
    }
    // 开始监听传入连接
    if (listen(listenSockRef, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSockRef);
        listenSockRef = INVALID_SOCKET;
        return false;
    }

    running_.store(true);
    acceptThread_ = std::thread(&ChatServer::acceptLoop, this);
    return true;
}

/**
 * 停止服务器，关闭所有连接
 */
void ChatServer::stop() {
    if (!running_.load()) return;  // 如果未运行则直接返回

    // 停止接受新连接
    running_.store(false);
    if (listenSock_ != INVALID_SOCKET) {
        closesocket(listenSock_);  // 关闭监听套接字
        listenSock_ = INVALID_SOCKET;
    }

    if (acceptThread_.joinable()) acceptThread_.join();  // 等待接受线程结束

    // 关闭所有客户端连接（避免在持锁时 delete 导致死锁）
    std::vector<ClientSession*> toClose;
    {
        std::lock_guard<std::mutex> lock(clientsMtx_);
        toClose.swap(clients_);  // 将当前列表转移出来并清空服务器持有的列表
    }
    for (auto* c : toClose) {
        c->forceClose();
    }
    for (auto* c : toClose) {
        delete c;  // 析构中 join 线程
    }
}

/**
 * 广播消息到所有客户端，排除指定客户端
 * @param type 消息类型
 * @param payload 消息负载
 * @param exclude 排除的客户端指针（可选）
 */
void ChatServer::broadcast(MsgType type, const std::string& payload,
                           ClientSession* exclude) {
    // 上锁保护客户端列表，并在锁外删除对象避免死锁
    std::vector<ClientSession*> toRemove;
    {
        std::lock_guard<std::mutex> lock(clientsMtx_);
        for (auto it = clients_.begin(); it != clients_.end();) {
            ClientSession* c = *it;
            if (exclude && c == exclude) {
                ++it;
                continue;
            }
            if (!sendFrame(c->sock(), type, payload)) {
                // 如果发送失败，直接从列表中移除，稍后在锁外关闭并析构
                it = clients_.erase(it);
                toRemove.push_back(c);
            } else {
                ++it;
            }
        }
    }
    // 在锁外进行强制关闭 + delete，避免析构中 join() 与持锁引发的死锁
    for (auto* c : toRemove) {
        c->forceClose();
    }
    for (auto* c : toRemove) {
        delete c;
    }
}

/**
 * 移除指定的客户端会话
 * @param c 要移除的客户端会话指针
 */
void ChatServer::removeClient(ClientSession* c) {
    // 上锁
    std::lock_guard<std::mutex> lock(clientsMtx_);
    // 查找并移除客户端
    auto it = std::find(clients_.begin(), clients_.end(), c);
    if (it != clients_.end()) clients_.erase(it);
}

/**
 * 持续监听客户端连接请求，接受新的链接请求并创建对应的Session
 */
void ChatServer::acceptLoop() {
    while (running_.load()) {
        // 开启新连接
        sockaddr_in caddr{};
        int clen = sizeof(caddr);
        SOCKET cs = accept(listenSock_, (sockaddr*)&caddr, &clen);
        if (cs == INVALID_SOCKET) {
            // 如果重连接失败且服务器正在运行，则继续循环
            if (!running_.load()) break;
            continue;
        }
        auto* cli = new ClientSession(this, cs);  // 创建新的客户端会话
        {
            // 上锁，加入客户端列表
            std::lock_guard<std::mutex> lock(clientsMtx_);
            clients_.push_back(cli);
        }
        cli->start();  // 启动客户端会话线程
    }
}

/**
 * 客户端会话析构函数，确保线程结束
 */
ClientSession::~ClientSession() {
    if (thread_.joinable()) thread_.join();
}

/**
 * 启动客户端会话线程
 */
void ClientSession::start() {
    thread_ = std::thread(&ClientSession::run, this);
}

void ClientSession::run() {
    // 首先从客户端 接收 HELLO 消息并获取昵称
    MsgType t;
    std::string p;
    if (!recvFrame(sock_.load(), t, p) || t != MsgType::HELLO) {
        // 接收失败或消息类型不对则关闭连接
        SOCKET s = sock_.exchange(INVALID_SOCKET);
        if (s != INVALID_SOCKET) closesocket(s);
        return;
    }
    nickname_ = p;

    // 通知所有客户端有新用户加入
    server_->broadcast(MsgType::USER_JOIN, nickname_, nullptr);

    // 持续接收客户端消息
    while (true) {
        MsgType type;
        std::string payload;
        // 当服务端被停止时，sock_ 会被置为 INVALID_SOCKET，从而使 recvFrame
        // 失败
        if (!recvFrame(sock_.load(), type, payload))
            break;  // 接收失败则退出循环
        if (type == MsgType::CHAT) {
            // 广播聊天消息，格式为 "昵称\n消息内容"
            std::string combined = nickname_;
            combined.push_back('\n');
            combined += payload;
            server_->broadcast(MsgType::SERVER_BROADCAST, combined, nullptr);
        } else if (type == MsgType::BYE) {
            // 客户端断开连接
            if (!payload.empty()) nickname_ = payload;
            break;
        }
    }

    // 通知所有客户端有用户离开
    server_->broadcast(MsgType::USER_LEAVE, nickname_, this);

    // 从服务器移除当前对话并关闭连接
    server_->removeClient(this);
    {
        // 将套接字设置为 INVALID_SOCKET
        SOCKET s = sock_.exchange(INVALID_SOCKET);
        // 如果原来的不是 INVALID_SOCKET
        if (s != INVALID_SOCKET)
            closesocket(s);  // 关闭客户端的通信套接字，释放资源
    }
}

void ClientSession::forceClose() {
    // 原子交换句柄，确保只关闭一次
    SOCKET s = sock_.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET) {
        shutdown(s, SD_BOTH);  // 关闭连接
        closesocket(s);        // 关闭原来的套接字，释放资源
    }
}
