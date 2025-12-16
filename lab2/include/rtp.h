#pragma once

#pragma once

#include <cstdint>
#include <vector>
#include <chrono>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;

inline bool socket_valid(socket_t s) {
    return s != INVALID_SOCKET;
}


namespace rtp {

constexpr std::size_t MAX_PAYLOAD = 1000;
constexpr uint16_t FLAG_SYN = 0x01;
constexpr uint16_t FLAG_ACK = 0x02;
constexpr uint16_t FLAG_FIN = 0x04;
constexpr uint16_t FLAG_DATA = 0x08;

constexpr int HANDSHAKE_TIMEOUT_MS = 800;
constexpr int DATA_TIMEOUT_MS = 500;

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t seq;
    uint32_t ack;
    uint16_t wnd;
    uint16_t len;
    uint16_t flags;
    uint32_t sack_mask;
    uint16_t checksum;
};
#pragma pack(pop)

struct Packet {
    PacketHeader header{};
    std::vector<uint8_t> payload;
};

uint16_t compute_checksum(const uint8_t* data, std::size_t len);
bool verify_checksum(const uint8_t* data, std::size_t len);
std::vector<uint8_t> serialize_packet(const PacketHeader& header, const std::vector<uint8_t>& payload);
bool parse_packet(const uint8_t* data, std::size_t len, Packet& out);

int init_socket_lib();
void close_socket(socket_t s);

uint64_t now_ms();
std::string addr_to_string(const sockaddr_in& addr);

}  // namespace rtp
