#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

#include "common/protocol.h"

// 负责网络连接/收发的客户端类（与 UI 解耦，通过回调输出文本）
class ChatClientNetwork {
public:
    using AppendFn = std::function<void(const std::wstring&)>;

    ChatClientNetwork() = default;
    ~ChatClientNetwork() { disconnect(); }

    void setAppendCallback(AppendFn fn) { append_ = std::move(fn); }

    bool connectTo(const std::wstring& addrW, const std::wstring& portW, const std::wstring& nickW);
    void disconnect();
    bool sendText(const std::wstring& textW);
    bool isConnected() const { return connected_.load(); }

private:
    void receiverLoop();
    void append(const std::wstring& w);

private:
    SOCKET sock_ { INVALID_SOCKET };
    std::thread recvThread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> disconnecting_{false};
    std::string nicknameUtf8_;
    AppendFn append_;
};

