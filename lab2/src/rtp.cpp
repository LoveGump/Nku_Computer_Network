#include "rtp.h"

#include <cstring>
#include <iostream>

namespace rtp
{

    uint16_t compute_checksum(const uint8_t *data, std::size_t len)
    {
        uint32_t sum = 0;
        std::size_t i = 0;
        while (i + 1 < len)
        {
            uint16_t word = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
            sum += word;
            sum = (sum & 0xFFFF) + (sum >> 16);
            i += 2;
        }
        if (i < len)
        {
            uint16_t word = static_cast<uint16_t>(data[i] << 8);
            sum += word;
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        while (sum >> 16)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return static_cast<uint16_t>(~sum);
    }

    bool verify_checksum(const uint8_t *data, std::size_t len)
    {
        return compute_checksum(data, len) == 0;
    }

    std::vector<uint8_t> serialize_packet(const PacketHeader &header, const std::vector<uint8_t> &payload)
    {
        PacketHeader net = header;
        net.seq = htonl(header.seq);
        net.ack = htonl(header.ack);
        net.wnd = htons(header.wnd);
        net.len = htons(header.len);
        net.flags = htons(header.flags);
        net.sack_mask = htonl(header.sack_mask);
        net.checksum = 0;

        std::vector<uint8_t> buffer(sizeof(PacketHeader) + payload.size());
        std::memcpy(buffer.data(), &net, sizeof(PacketHeader));
        if (!payload.empty())
        {
            std::memcpy(buffer.data() + sizeof(PacketHeader), payload.data(), payload.size());
        }
        uint16_t cs = compute_checksum(buffer.data(), buffer.size());
        auto *hdr = reinterpret_cast<PacketHeader *>(buffer.data());
        hdr->checksum = htons(cs);
        return buffer;
    }

    bool parse_packet(const uint8_t *data, std::size_t len, Packet &out)
    {
        if (len < sizeof(PacketHeader))
        {
            return false;
        }
        PacketHeader net{};
        std::memcpy(&net, data, sizeof(PacketHeader));
        if (!verify_checksum(data, len))
        {
            return false;
        }

        out.header.seq = ntohl(net.seq);
        out.header.ack = ntohl(net.ack);
        out.header.wnd = ntohs(net.wnd);
        out.header.len = ntohs(net.len);
        out.header.flags = ntohs(net.flags);
        out.header.sack_mask = ntohl(net.sack_mask);
        out.header.checksum = ntohs(net.checksum);

        if (out.header.len + sizeof(PacketHeader) != len)
        {
            return false;
        }
        out.payload.clear();
        if (out.header.len > 0)
        {
            out.payload.insert(out.payload.end(), data + sizeof(PacketHeader), data + len);
        }
        return true;
    }

    int init_socket_lib()
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            std::cerr << "WSAStartup failed\n";
            return -1;
        }
        return 0;
    }

    void close_socket(socket_t s)
    {
        closesocket(s);
    }

    uint64_t now_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::string addr_to_string(const sockaddr_in &addr)
    {
        char buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &(addr.sin_addr), buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
    }

} // namespace rtp
