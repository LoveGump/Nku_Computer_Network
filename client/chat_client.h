#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

#include "common/protocol.h"

/**
 * 客户端网络类，管理与聊天服务器的连接和通信
 */
class ChatClientNetwork {
public:
    using AppendFn = std::function<void(const std::wstring&)>;
    using StateFn  = std::function<void(bool connected)>;

    ChatClientNetwork() = default;
    ~ChatClientNetwork() { disconnect(); }// 确保析构时断开连接

    void setAppendCallback(AppendFn fn) { append_ = std::move(fn); }
    void setStateCallback(StateFn fn) { state_ = std::move(fn); }

    bool connectTo(const std::wstring& addrW, const std::wstring& portW, const std::wstring& nickW);
    void disconnect();
    bool sendText(const std::wstring& textW);
    bool isConnected() const { return connected_.load(); }

private:
    void receiverLoop();
    void append(const std::wstring& w);
    void notifyState(bool connected) { if (state_) state_(connected); }

private:
    SOCKET sock_ { INVALID_SOCKET };
    std::thread recvThread_; // 接收消息线程
    std::atomic<bool> connected_{false}; // 连接状态
    std::atomic<bool> disconnecting_{false}; // 正在断开连接
    std::string nicknameUtf8_; // 用户昵称（UTF-8 编码）
    AppendFn append_; // 用于显示消息的回调函数
    StateFn state_;   // 连接状态变化回调
};

