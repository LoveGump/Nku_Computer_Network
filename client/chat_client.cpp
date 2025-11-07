#include "chat_client.h"

using namespace chatproto;

/**
 * 向服务端发起连接请求
 * @param addrW 服务器地址（UTF-16）
 * @param portW 服务器端口（UTF-16）
 * @param nickW 昵称（UTF-16）
 * @return 连接是否成功
 * @note 首先清理旧连接，然后解析地址并尝试连接，连接成功后发送 HELLO
 * 消息，启用recive线程
 */
bool ChatClientNetwork::connectTo(const std::wstring& addrW,
                                  const std::wstring& portW,
                                  const std::wstring& nickW) {
    if (connected_.load()) return true;  // 已连接则直接返回 true

    // 若上一次是被动断开，接收线程可能已退出但仍处于 joinable 状态；先回收
    if (recvThread_.joinable()) {
        recvThread_.join();
    }
    // 清理残留的旧 socket（若有）
    if (sock_ != INVALID_SOCKET) {
        shutdown(sock_, SD_BOTH);
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    std::wstring waddr = addrW.empty() ? L"127.0.0.1" : addrW;
    std::wstring wport = portW.empty() ? L"5000" : portW;
    std::wstring wnick = nickW.empty() ? L"User" : nickW;

    // 转换为 UTF-8
    std::string addr = utf16_to_utf8(waddr);
    std::string port = utf16_to_utf8(wport);
    nicknameUtf8_ = utf16_to_utf8(wnick);

    // 解析地址
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // 获取地址信息
    addrinfo* res = nullptr;
    if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) != 0) {
        append(L"[错误] 解析地址失败\r\n");
        return false;
    }

    // 连接到服务器
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
            // 如果连接失败，关闭套接字并尝试下一个地址
            closesocket(s);
            s = INVALID_SOCKET;
            continue;
        }
        break;
    }
    // 释放getaddrinfo分配的内存
    freeaddrinfo(res);

    if (s == INVALID_SOCKET) {
        // 如果到最后还是INVALID_SOCKET 说明链接失败
        append(L"[错误] 无法连接服务器\r\n");
        return false;
    }

    // 发送 HELLO 消息
    if (!sendFrame(s, MsgType::HELLO, nicknameUtf8_)) {
        // 如果发送失败，关闭套接字并返回
        closesocket(s);
        append(L"[错误] 发送 HELLO 失败\r\n");
        return false;
    }

    // 连接成功
    sock_ = s;
    connected_.store(true);
    disconnecting_.store(false);
    append(L"[系统] 已连接\r\n");  // 提示已连接
    notifyState(true);             // 状态通知
    recvThread_ = std::thread(&ChatClientNetwork::receiverLoop, this);
    return true;
}

/**
 * 断开与服务器的连接
 * @note 幂等断开：即便已被动断开也需要 join 线程，避免析构时重复join()导致的
 * terminate。
 */
void ChatClientNetwork::disconnect() {
    disconnecting_.store(true);

    bool wasConnected = connected_.exchange(false);  // 获取并清除连接状态
    if (wasConnected && sock_ != INVALID_SOCKET) {
        // 仍处于连接：尝试发送 BYE（忽略失败）再关闭
        sendFrame(sock_, MsgType::BYE, nicknameUtf8_);
        shutdown(sock_, SD_BOTH);
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    // 被动断开后线程可能已退出但未 join
    if (recvThread_.joinable()) {
        recvThread_.join();
    }

    disconnecting_.store(false);
    notifyState(false);  // 状态通知幂等
}

/**
 * 发送聊天文本
 * @param textW 聊天文本（UTF-16）
 */
bool ChatClientNetwork::sendText(const std::wstring& textW) {
    if (!connected_.load()) return false;
    std::string utf8 = utf16_to_utf8(textW);
    return sendFrame(sock_, MsgType::CHAT, utf8);
}

/**
 *  接收消息线程，循环接收新的消息
 *  @note 根据消息类型调用不同的回调函数处理
 */
void ChatClientNetwork::receiverLoop() {
    while (true) {
        MsgType t;
        std::string p;
        // 接收消息失败则退出循环，一般是连接断开
        if (!recvFrame(sock_, t, p)) break;
        switch (t) {
            case MsgType::USER_JOIN: {
                std::wstring msg = L"[加入] ";
                msg += utf8_to_utf16(p);
                msg += L"\r\n";
                append(msg);
                break;
            }
            case MsgType::USER_LEAVE: {
                std::wstring msg = L"[离开] ";
                msg += utf8_to_utf16(p);
                msg += L"\r\n";
                append(msg);
                break;
            }
            case MsgType::SERVER_BROADCAST: {
                size_t pos = p.find('\n');
                std::string from =
                    pos == std::string::npos ? std::string() : p.substr(0, pos);
                std::string text =
                    pos == std::string::npos ? p : p.substr(pos + 1);
                std::wstring line = L"<" + utf8_to_utf16(from) + L"> " +
                                    utf8_to_utf16(text) + L"\r\n";
                append(line);
                break;
            }
            default:
                break;
        }
    }

    // 断开连接：若是被动断开，更新连接状态并提示
    if (!disconnecting_.load()) {
        connected_.store(false);
        notifyState(false);
        append(L"[系统] 已断开连接\r\n");
    }
}

/**
 * 调用回调函数显示消息
 * @param w 消息内容（UTF-16）
 */
void ChatClientNetwork::append(const std::wstring& w) {
    if (append_) append_(w);
}
