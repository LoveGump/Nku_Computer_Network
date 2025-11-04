#include "chat_client.h"

using namespace chatproto;

bool ChatClientNetwork::connectTo(const std::wstring& addrW, const std::wstring& portW, const std::wstring& nickW) {
    if (connected_.load()) return true;

    std::wstring waddr = addrW.empty() ? L"127.0.0.1" : addrW;
    std::wstring wport = portW.empty() ? L"5000" : portW;
    std::wstring wnick = nickW.empty() ? L"User" : nickW;

    std::string addr = utf16_to_utf8(waddr);
    std::string port = utf16_to_utf8(wport);
    nicknameUtf8_ = utf16_to_utf8(wnick);

    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) != 0) {
        append(L"[错误] 解析地址失败\r\n");
        return false;
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
            closesocket(s); s = INVALID_SOCKET; continue;
        }
        break;
    }
    freeaddrinfo(res);

    if (s == INVALID_SOCKET) {
        append(L"[错误] 无法连接服务器\r\n");
        return false;
    }

    if (!sendFrame(s, MsgType::HELLO, nicknameUtf8_)) {
        closesocket(s);
        append(L"[错误] 发送 HELLO 失败\r\n");
        return false;
    }

    sock_ = s;
    connected_.store(true);
    disconnecting_.store(false);
    append(L"[系统] 已连接\r\n");
    recvThread_ = std::thread(&ChatClientNetwork::receiverLoop, this);
    return true;
}

void ChatClientNetwork::disconnect() {
    if (!connected_.load()) return;
    disconnecting_.store(true);
    // 发送 BYE
    sendFrame(sock_, MsgType::BYE, nicknameUtf8_);

    connected_.store(false);
    shutdown(sock_, SD_BOTH);
    closesocket(sock_);
    if (recvThread_.joinable()) recvThread_.join();
    sock_ = INVALID_SOCKET;
    disconnecting_.store(false);
}

bool ChatClientNetwork::sendText(const std::wstring& textW) {
    if (!connected_.load()) return false;
    std::string utf8 = utf16_to_utf8(textW);
    return sendFrame(sock_, MsgType::CHAT, utf8);
}

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
    if (!disconnecting_.load()) {
        append(L"[系统] 已断开连接\r\n");
    }
}

void ChatClientNetwork::append(const std::wstring& w) {
    if (append_) append_(w);
}
