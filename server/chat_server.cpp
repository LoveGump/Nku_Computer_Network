#include "chat_server.h"
#include <algorithm>
#include <iostream>

using namespace chatproto;

/**
 * 启动服务器，监听指定端口
 * @param port 监听端口
 */
bool ChatServer::start(uint16_t port) {
    if (running_.load()) return true; // 如果已经运行则直接返回 true

    SOCKET& listenSockRef = listenSock_;
    listenSockRef = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSockRef == INVALID_SOCKET) return false;

    u_long yes = 1;
    setsockopt(listenSockRef, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenSockRef, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenSockRef); listenSockRef = INVALID_SOCKET; return false;
    }
    if (listen(listenSockRef, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSockRef); listenSockRef = INVALID_SOCKET; return false;
    }

    running_.store(true);
    acceptThread_ = std::thread(&ChatServer::acceptLoop, this);
    return true;
}

void ChatServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (listenSock_ != INVALID_SOCKET) {
        closesocket(listenSock_);
        listenSock_ = INVALID_SOCKET;
    }
    if (acceptThread_.joinable()) acceptThread_.join();

    // close all clients
    std::lock_guard<std::mutex> lock(clientsMtx_);
    for (auto* c : clients_) {
        closesocket(c->sock());
        delete c;
    }
    clients_.clear();
}

/**
 * 广播消息到所有客户端，排除指定客户端（如果有）
 * @param type 消息类型
 * @param payload 消息负载
 * @param exclude 排除的客户端指针（可选）
 */
void ChatServer::broadcast(MsgType type, const std::string& payload, ClientSession* exclude) {
    std::lock_guard<std::mutex> lock(clientsMtx_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        ClientSession* c = *it;
        if (exclude && c == exclude) { ++it; continue; }
        if (!sendFrame(c->sock(), type, payload)) {
            closesocket(c->sock());
            it = clients_.erase(it);
            delete c;
        } else {
            ++it;
        }
    }
}


void ChatServer::removeClient(ClientSession* c) {
    std::lock_guard<std::mutex> lock(clientsMtx_);
    auto it = std::find(clients_.begin(), clients_.end(), c);
    if (it != clients_.end()) clients_.erase(it);
}

void ChatServer::acceptLoop() {
    while (running_.load()) {
        sockaddr_in caddr{}; int clen = sizeof(caddr);
        SOCKET cs = accept(listenSock_, (sockaddr*)&caddr, &clen);
        if (cs == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }
        auto* cli = new ClientSession(this, cs);
        {
            std::lock_guard<std::mutex> lock(clientsMtx_);
            clients_.push_back(cli);
        }
        cli->start();
    }
}

ClientSession::~ClientSession() {
    if (thread_.joinable()) thread_.join();
}

void ClientSession::start() {
    thread_ = std::thread(&ClientSession::run, this);
}

void ClientSession::run() {
    // Expect HELLO
    MsgType t; std::string p;
    if (!recvFrame(sock_, t, p) || t != MsgType::HELLO) {
        closesocket(sock_); return;
    }
    nickname_ = p;

    // announce join
    server_->broadcast(MsgType::USER_JOIN, nickname_, nullptr);

    while (true) {
        MsgType type; std::string payload;
        if (!recvFrame(sock_, type, payload)) break;
        if (type == MsgType::CHAT) {
            std::string combined = nickname_;
            combined.push_back('\n');
            combined += payload;
            server_->broadcast(MsgType::SERVER_BROADCAST, combined, nullptr);
        } else if (type == MsgType::BYE) {
            if (!payload.empty()) nickname_ = payload;
            break;
        }
    }

    server_->broadcast(MsgType::USER_LEAVE, nickname_, this);
    server_->removeClient(this);
    closesocket(sock_);
}
