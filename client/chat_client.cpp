#include "chat_client.h"

using namespace chatproto;

/**
 * 连接到服务器
 * @param addrW 服务器地址（UTF-16）
 * @param portW 服务器端口（UTF-16）
 * @param nickW 昵称（UTF-16）
 */
bool ChatClientNetwork::connectTo(const std::wstring& addrW, const std::wstring& portW, const std::wstring& nickW) {
    if (connected_.load()) return true; // 已连接则直接返回 true

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
            closesocket(s); s = INVALID_SOCKET; continue;
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
        closesocket(s);
        append(L"[错误] 发送 HELLO 失败\r\n");
        return false;
    }

    // 连接成功
    sock_ = s;
    connected_.store(true);
    disconnecting_.store(false);
    append(L"[系统] 已连接\r\n");
    recvThread_ = std::thread(&ChatClientNetwork::receiverLoop, this);
    return true;
}

/**
 * 断开与服务器的连接
 */
void ChatClientNetwork::disconnect() {
    if (!connected_.load()) return;
    disconnecting_.store(true);
    // 发送 BYE
    sendFrame(sock_, MsgType::BYE, nicknameUtf8_);

    // 清理资源
    connected_.store(false);
    shutdown(sock_, SD_BOTH);
    closesocket(sock_);
    if (recvThread_.joinable()) recvThread_.join();
    sock_ = INVALID_SOCKET;
    disconnecting_.store(false);
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
 * 接收消息循环
 */
void ChatClientNetwork::receiverLoop() {
    while (true) {
        MsgType t; std::string p;
        if (!recvFrame(sock_, t, p)) break;
        if (t == MsgType::USER_JOIN) {
            std::wstring msg = L"[加入] ";
            msg += utf8_to_utf16(p);
            msg += L"\r\n";
            append(msg);
        } else if (t == MsgType::USER_LEAVE) {
            std::wstring msg = L"[离开] ";
            msg += utf8_to_utf16(p);
            msg += L"\r\n";
            append(msg);
        } else if (t == MsgType::SERVER_BROADCAST) {
            size_t pos = p.find('\n');
            std::string from = pos == std::string::npos ? std::string() : p.substr(0, pos);
            std::string text = pos == std::string::npos ? p : p.substr(pos + 1);
            std::wstring line = L"<" + utf8_to_utf16(from) + L"> " + utf8_to_utf16(text) + L"\r\n";
            append(line);
        }
    }
    
    // 断开连接
    if (!disconnecting_.load()) {
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
