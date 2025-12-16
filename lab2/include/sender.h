#pragma once

#include "rtp.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rtp
{

    class ReliableSender
    {
    public:
        ReliableSender(std::string dest_ip, uint16_t dest_port, std::string file_path, uint16_t window_size, uint16_t local_port = 0);
        ~ReliableSender();

        int run();

    private:
        struct SegmentInfo
        {
            std::vector<uint8_t> data;
            bool sent{false};
            bool acked{false};
            uint64_t last_send{0};
            bool sack_missing{false};
        };

        struct TxState
        {
            uint32_t base_seq{1};
            uint32_t next_seq{1};
            uint32_t total_segments{0};
            uint32_t dup_ack_count{0};
            double cwnd{1.0};
            double ssthresh{16.0};
            bool in_fast_recovery{false};
            uint16_t peer_wnd{0};
        };

        bool wait_for_packet(Packet &pkt, sockaddr_in &from, int timeout_ms);
        int send_raw(const PacketHeader &hdr, const std::vector<uint8_t> &payload);
        bool handshake();
        void retransmit(uint32_t seq, SegmentInfo &seg);
        std::size_t inflight_count() const;
        void mark_acked(uint32_t seq);
        void handle_ack(const Packet &pkt, bool &fast_retx_needed);
        bool all_acked() const;
        void handle_timeouts();
        void try_send_data();
        void try_send_fin();
        void process_network();

        socket_t sock_{INVALID_SOCKET_VALUE};
        sockaddr_in remote_{};
        std::string dest_ip_;
        uint16_t dest_port_{0};
        uint16_t local_port_{0};
        std::string file_path_;
        uint16_t window_size_{0};

        std::vector<SegmentInfo> segments_;
        TxState state_{};
        bool fin_sent_{false};
        bool fin_complete_{false};
        uint64_t fin_last_send_{0};
        int fin_retry_count_{0};
        std::vector<uint8_t> file_data_;

        uint64_t start_time_{0};
        uint64_t end_time_{0};
        bool data_timing_recorded_{false};

        // 统计信息
        uint32_t retransmit_count_{0};
        uint32_t timeout_count_{0};
        uint32_t fast_retransmit_count_{0};
    };

} // namespace rtp
