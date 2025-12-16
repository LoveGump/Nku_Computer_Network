#pragma once

#include "rtp.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtp
{

    struct RecvState
    {
        uint32_t expected_seq{1}; // 下一个期望的序号
        uint16_t window_size{0};    // 接收窗口大小
        std::map<uint32_t, std::vector<uint8_t>> buffer; // 已接收但未按序写入的数据包缓冲区
    };

    class ReliableReceiver
    {
    public:
        ReliableReceiver(uint16_t listen_port, std::string output_path, uint16_t window_size);
        ~ReliableReceiver();

        int run();

    private:
        bool wait_for_packet(Packet &pkt, sockaddr_in &from, int timeout_ms);
        int send_raw(const PacketHeader &hdr, const std::vector<uint8_t> &payload);
        uint32_t build_sack_mask() const;
        void send_ack(bool fin = false, uint32_t fin_ack = 0);
        bool do_handshake();

        socket_t sock_{INVALID_SOCKET_VALUE};
        uint16_t listen_port_{0};
        std::string output_path_;
        uint16_t window_size_{0};
        sockaddr_in client_{};
        RecvState state_{};

        uint64_t start_time_{0};
        uint64_t end_time_{0};
        std::size_t bytes_written_{0};

        uint32_t total_packets_received_{0};
        uint32_t duplicate_packets_{0};
        uint32_t out_of_order_packets_{0};
    };

} // namespace rtp
