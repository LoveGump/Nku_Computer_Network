#include "rtp.h"

#include <cstring>
#include <iostream>
#include <random>

namespace rtp {
    using std::cerr;
    using std::size_t;
    using std::vector;
    namespace {
        uint32_t fnv1a(const uint8_t* data, size_t len) {
            constexpr uint32_t FNV_PRIME = 16777619u;
            uint32_t hash = 2166136261u;
            for (size_t i = 0; i < len; ++i) {
                hash ^= static_cast<uint32_t>(data[i]);
                hash *= FNV_PRIME;
            }
            return hash;
        }

        uint64_t secret_salt() {
            static const uint64_t salt = []() {
                std::random_device rd;
                uint64_t high = static_cast<uint64_t>(rd()) << 32;
                uint64_t low = static_cast<uint64_t>(rd());
                return high ^ low;
            }();
            return salt;
        }
    }  // namespace

    uint16_t compute_checksum(const uint8_t* data, size_t len) {
        uint32_t sum = 0;
        size_t i = 0;
        while (i + 1 < len) {
            uint16_t word = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
            sum += word;
            sum = (sum & 0xFFFF) + (sum >> 16);
            i += 2;
        }
        if (i < len) {
            uint16_t word = static_cast<uint16_t>(data[i] << 8);
            sum += word;
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return static_cast<uint16_t>(~sum);
    }

    bool verify_checksum(const uint8_t* data, size_t len) {
        return compute_checksum(data, len) == 0;
    }

    vector<uint8_t> serialize_packet(const PacketHeader& header,
                                     const vector<uint8_t>& payload) {
        PacketHeader net = header;
        net.seq = htonl(header.seq);
        net.ack = htonl(header.ack);
        net.wnd = htons(header.wnd);
        net.len = htons(header.len);
        net.flags = htons(header.flags);
        net.sack_mask = htonl(header.sack_mask);
        net.checksum = 0;

        vector<uint8_t> buffer(sizeof(PacketHeader) + payload.size());
        memcpy(buffer.data(), &net, sizeof(PacketHeader));
        if (!payload.empty()) {
            memcpy(buffer.data() + sizeof(PacketHeader), payload.data(),
                   payload.size());
        }
        uint16_t cs = compute_checksum(buffer.data(), buffer.size());
        auto* hdr = reinterpret_cast<PacketHeader*>(buffer.data());
        hdr->checksum = htons(cs);
        return buffer;
    }

    bool parse_packet(const uint8_t* data, size_t len, Packet& out) {
        if (len < sizeof(PacketHeader)) {
            return false;
        }
        PacketHeader net{};
        memcpy(&net, data, sizeof(PacketHeader));
        if (!verify_checksum(data, len)) {
            return false;
        }

        out.header.seq = ntohl(net.seq);
        out.header.ack = ntohl(net.ack);
        out.header.wnd = ntohs(net.wnd);
        out.header.len = ntohs(net.len);
        out.header.flags = ntohs(net.flags);
        out.header.sack_mask = ntohl(net.sack_mask);
        out.header.checksum = ntohs(net.checksum);

        if (out.header.len + sizeof(PacketHeader) != len) {
            return false;
        }
        out.payload.clear();
        if (out.header.len > 0) {
            out.payload.insert(out.payload.end(), data + sizeof(PacketHeader),
                               data + len);
        }
        return true;
    }

    uint32_t generate_isn(const sockaddr_in& local, const sockaddr_in& remote) {
        uint8_t tuple_buf[12] = {0};
        memcpy(tuple_buf, &local.sin_addr.s_addr,
               sizeof(local.sin_addr.s_addr));
        memcpy(tuple_buf + 4, &local.sin_port, sizeof(local.sin_port));
        memcpy(tuple_buf + 6, &remote.sin_addr.s_addr,
               sizeof(remote.sin_addr.s_addr));
        memcpy(tuple_buf + 10, &remote.sin_port, sizeof(remote.sin_port));

        uint64_t salt = secret_salt();
        uint8_t salt_buf[8] = {0};
        memcpy(salt_buf, &salt, sizeof(salt_buf));
        uint32_t hash = fnv1a(tuple_buf, sizeof(tuple_buf));
        hash ^= fnv1a(salt_buf, sizeof(salt_buf));

        uint32_t counter = static_cast<uint32_t>(now_ms());
        return hash + counter;
    }

    int init_socket_lib() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            cerr << "WSAStartup failed\n";
            return -1;
        }
        return 0;
    }

    void close_socket(socket_t s) { closesocket(s); }

    uint64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch())
            .count();
    }

    string addr_to_string(const sockaddr_in& addr) {
        char* ip_str = inet_ntoa(addr.sin_addr);
        return string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
    }

}  // namespace rtp
