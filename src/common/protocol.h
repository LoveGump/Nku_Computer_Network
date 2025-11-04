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

// Default server port
static constexpr uint16_t DEFAULT_PORT = 5000;
// Max payload length (64 KiB)
static constexpr uint32_t MAX_PAYLOAD = 64 * 1024;

// Message types
enum class MsgType : uint8_t {
    HELLO = 0x01,          // C->S: payload = UTF-8 nickname
    CHAT = 0x02,           // C->S: payload = UTF-8 text
    BYE  = 0x03,           // C->S: payload = UTF-8 nickname (optional); client intends to disconnect

    USER_JOIN = 0x11,      // S->C: payload = UTF-8 nickname
    USER_LEAVE = 0x12,     // S->C: payload = UTF-8 nickname
    SERVER_BROADCAST = 0x13// S->C: payload = UTF-8: from + '\n' + text
};

inline bool sendAll(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        sent += n;
    }
    return true;
}

inline bool recvAll(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, buf + got, len - got, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        got += n;
    }
    return true;
}

inline bool sendFrame(SOCKET s, MsgType type, const std::string& payload) {
    if (payload.size() > MAX_PAYLOAD) return false;
    uint8_t header[5];
    header[0] = static_cast<uint8_t>(type);
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(header + 1, &len, 4);
    if (!sendAll(s, reinterpret_cast<const char*>(header), 5)) return false;
    if (!payload.empty()) {
        if (!sendAll(s, payload.data(), static_cast<int>(payload.size()))) return false;
    }
    return true;
}

inline bool recvFrame(SOCKET s, MsgType& typeOut, std::string& payloadOut) {
    uint8_t header[5];
    if (!recvAll(s, reinterpret_cast<char*>(header), 5)) return false;
    typeOut = static_cast<MsgType>(header[0]);
    uint32_t nlen = 0;
    std::memcpy(&nlen, header + 1, 4);
    uint32_t len = ntohl(nlen);
    if (len > MAX_PAYLOAD) return false;
    payloadOut.clear();
    if (len == 0) return true;
    payloadOut.resize(len);
    return recvAll(s, payloadOut.data(), static_cast<int>(len));
}
inline std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), size, nullptr, nullptr);
    return out;
}

inline std::wstring utf8_to_utf16(const std::string& s) {
    if (s.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), size);
    return out;
}

} // namespace chatproto
