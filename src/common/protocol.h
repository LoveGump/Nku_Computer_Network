#pragma once

// 基础的 TCP 聊天协议实用程序（仅限头文件以简化）
// 帧格式: [1字节类型][4字节负载长度大端][负载字节]
// 所有负载中的字符串均为 UTF-8。

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <cstring>

namespace chatproto {

// 默认服务器端口
static constexpr uint16_t DEFAULT_PORT = 5000;
// 最大负载长度（64 KiB）
static constexpr uint32_t MAX_PAYLOAD = 64 * 1024;

// 消息类型枚举
enum class MsgType : uint8_t {
    HELLO = 0x01,          // C->S: payload = UTF-8 昵称
    CHAT = 0x02,           // C->S: payload = UTF-8 文本
    BYE  = 0x03,           // C->S: payload = UTF-8 昵称 (可选); 客户端打算断开连接

    USER_JOIN = 0x11,      // S->C: payload = UTF-8 昵称
    USER_LEAVE = 0x12,     // S->C: payload = UTF-8 昵称
    SERVER_BROADCAST = 0x13// S->C: payload = UTF-8: from + '\n' + text
};

/**
 * 发送所有数据的辅助函数
 * @param s 套接字
 * @param data 数据指针
 * @param len 数据长度
 */
inline bool sendAll(SOCKET s, const char* data, int len) {
    int sent = 0; // 已发送字节数
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        sent += n;
    }
    return true;
}

/**
 * 接收所有数据的辅助函数
 * @param s 套接字
 * @param buf 缓冲区指针
 * @param len 需要接收的字节数
 */
inline bool recvAll(SOCKET s, char* buf, int len) {
    int got = 0;    // 接收字节数
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        got += n;
    }
    return true;
}

/**
 * 发送数据帧
 * @param s 套接字
 * @param type 消息类型
 * @param payload 负载数据
 */
inline bool sendFrame(SOCKET s, MsgType type, const std::string& payload) {
    if (payload.size() > MAX_PAYLOAD) return false; // 检查负载大小
    uint8_t header[5]; // 帧头
    header[0] = static_cast<uint8_t>(type); // 消息类型
    // 负载长度（大端序）
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(header + 1, &len, 4);
    // 发送帧头
    if (!sendAll(s, reinterpret_cast<const char*>(header), 5)) return false;
    // 发送负载
    if (!payload.empty()) {
        if (!sendAll(s, payload.data(), static_cast<int>(payload.size()))) return false;
    }
    return true;
}

/**
 * 接收数据帧
 * @param s 套接字
 * @param typeOut 输出消息类型
 * @param payloadOut 输出负载数据
 */
inline bool recvFrame(SOCKET s, MsgType& typeOut, std::string& payloadOut) {
    // 先接收Frame Header
    uint8_t header[5];
    if (!recvAll(s, reinterpret_cast<char*>(header), 5)) return false;
    typeOut = static_cast<MsgType>(header[0]);// 解析消息类型
    // 解析负载长度
    uint32_t nlen = 0; 
    std::memcpy(&nlen, header + 1, 4);
    uint32_t len = ntohl(nlen); // 转换为主机字节序
    if (len > MAX_PAYLOAD) return false;
    payloadOut.clear();
    if (len == 0) return true;
    payloadOut.resize(len);
    return recvAll(s, payloadOut.data(), static_cast<int>(len));
}

/**
 * 将 UTF-16 字符串转换为 UTF-8 字符串
 * @param w UTF-16 字符串
 * @return 转换后的 UTF-8 字符串
 */
inline std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    // 计算所需缓冲区大小
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    // 预分配结果字符串
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), size, nullptr, nullptr);
    return out;
}

/**
 * 将 UTF-8 字符串转换为 UTF-16 字符串
 * @param s UTF-8 字符串
 * @return 转换后的 UTF-16 字符串
 */
inline std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) return std::wstring();
    // 计算所需缓冲区大小
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    // 预分配结果字符串
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), size);
    return out;
}

} // namespace chatproto
