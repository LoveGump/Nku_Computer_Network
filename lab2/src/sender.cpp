#include "sender.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace rtp
{
    namespace
    {
        constexpr std::size_t SACK_BITS = 32;
        constexpr int MAX_HANDSHAKE_RETRIES = 5;
        constexpr int MAX_FIN_RETRIES = 5;
        constexpr int MAX_SACK_RETX_PER_ACK = 4;
        constexpr int MIN_GAP_RETX_INTERVAL_MS = DATA_TIMEOUT_MS / 2;

        bool same_endpoint(const sockaddr_in &a, const sockaddr_in &b)
        {
            return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
        }
    } // namespace

    ReliableSender::ReliableSender(std::string dest_ip, uint16_t dest_port, std::string file_path, uint16_t window_size, uint16_t local_port)
        : dest_ip_(std::move(dest_ip)),
          dest_port_(dest_port),
          local_port_(local_port),
          file_path_(std::move(file_path)),
          window_size_(std::min<uint16_t>(window_size, static_cast<uint16_t>(SACK_BITS)))
    {
        remote_.sin_family = AF_INET;
        remote_.sin_port = htons(dest_port_);
    }

    ReliableSender::~ReliableSender()
    {
        if (socket_valid(sock_))
        {
            close_socket(sock_);
        }
    }

    bool ReliableSender::wait_for_packet(Packet &pkt, sockaddr_in &from, int timeout_ms)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_, &rfds);
        timeval tv{};
        if (timeout_ms >= 0)
        {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
        }
        int nfds = 0;
        int rv = select(nfds, &rfds, nullptr, nullptr, timeout_ms >= 0 ? &tv : nullptr);
        if (rv <= 0)
        {
            return false;
        }
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        std::vector<uint8_t> buf(2048);
        int n = recvfrom(sock_, reinterpret_cast<char *>(buf.data()), static_cast<int>(buf.size()), 0,
                         reinterpret_cast<sockaddr *>(&peer), &len);
        if (n <= 0)
        {
            return false;
        }
        if (!parse_packet(buf.data(), static_cast<std::size_t>(n), pkt))
        {
            return false;
        }
        from = peer;
        return true;
    }

    int ReliableSender::send_raw(const PacketHeader &hdr, const std::vector<uint8_t> &payload)
    {
        auto buffer = serialize_packet(hdr, payload);
        return sendto(sock_, reinterpret_cast<const char *>(buffer.data()), static_cast<int>(buffer.size()), 0,
                      reinterpret_cast<const sockaddr *>(&remote_), sizeof(remote_));
    }

    bool ReliableSender::handshake()
    {
        PacketHeader syn{};
        syn.seq = 0;
        syn.ack = 0;
        syn.wnd = window_size_;
        syn.len = 0;
        syn.flags = FLAG_SYN;
        syn.sack_mask = 0;

        uint16_t peer_wnd = window_size_;
        std::cout << "[DEBUG] Starting handshake with " << dest_ip_ << ":" << dest_port_ << "\n";
        for (int attempt = 0; attempt < MAX_HANDSHAKE_RETRIES; ++attempt)
        {
            std::cout << "[DEBUG] Sending SYN (attempt " << (attempt + 1) << "/" << MAX_HANDSHAKE_RETRIES << ")\n";
            send_raw(syn, {});
            Packet pkt{};
            sockaddr_in from{};
            if (!wait_for_packet(pkt, from, HANDSHAKE_TIMEOUT_MS))
            {
                continue;
            }
            if (!same_endpoint(from, remote_))
            {
                std::cout << "[DEBUG] Ignoring handshake response from unexpected peer\n";
                continue;
            }
            if ((pkt.header.flags & FLAG_SYN) && (pkt.header.flags & FLAG_ACK) && pkt.header.ack == syn.seq + 1)
            {
                peer_wnd = std::min<uint16_t>(pkt.header.wnd, static_cast<uint16_t>(SACK_BITS));
                std::cout << "[DEBUG] Received SYN+ACK, peer window size: " << peer_wnd << "\n";
                PacketHeader ack{};
                ack.seq = 1;
                ack.ack = pkt.header.seq + 1;
                ack.flags = FLAG_ACK;
                ack.wnd = window_size_;
                ack.len = 0;
                ack.sack_mask = 0;
                send_raw(ack, {});
                state_.peer_wnd = peer_wnd;
                std::cout << "[DEBUG] Handshake completed successfully\n";
                return true;
            }
        }
        std::cerr << "[WARN] Handshake failed after retries\n";
        return false;
    }

    void ReliableSender::retransmit(uint32_t seq, SegmentInfo &seg)
    {
        bool is_retransmit = seg.sent; // 濡傛灉宸茬粡鍙戦€佽繃锛岃繖鏄噸浼?
        PacketHeader hdr{};
        hdr.seq = seq;
        hdr.ack = 0;
        hdr.flags = FLAG_DATA;
        hdr.wnd = window_size_;
        hdr.len = static_cast<uint16_t>(seg.data.size());
        hdr.sack_mask = 0;
        if (start_time_ == 0)
        {
            start_time_ = now_ms();
        }
        send_raw(hdr, seg.data);
        seg.sent = true;
        seg.last_send = now_ms();

        if (is_retransmit)
        {
            retransmit_count_++;
        }
    }

    std::size_t ReliableSender::inflight_count() const
    {
        std::size_t count = 0;
        for (const auto &s : segments_)
        {
            if (s.sent && !s.acked)
            {
                ++count;
            }
        }
        return count;
    }

    void ReliableSender::mark_acked(uint32_t seq)
    {
        if (seq == 0 || seq > state_.total_segments)
        {
            return;
        }
        auto &seg = segments_[seq - 1];
        if (!seg.acked)
        {
            seg.acked = true;
            seg.sack_missing = false;
        }
    }

    void ReliableSender::handle_ack(const Packet &pkt, bool &fast_retx_needed)
    {
        state_.peer_wnd = std::min<uint16_t>(pkt.header.wnd, static_cast<uint16_t>(SACK_BITS));
        uint32_t ack = pkt.header.ack;

        if (ack > state_.base_seq)
        {
            for (uint32_t s = state_.base_seq; s < ack && s <= state_.total_segments; ++s)
            {
                mark_acked(s);
            }
            state_.base_seq = ack;
            state_.dup_ack_count = 0;
            if (state_.in_fast_recovery)
            {
                state_.cwnd = state_.ssthresh;
                state_.in_fast_recovery = false;
            }
            if (state_.cwnd < state_.ssthresh)
            {
                state_.cwnd += 1.0;
            }
            else
            {
                state_.cwnd += 1.0 / state_.cwnd;
            }
        }
        else if (ack == state_.base_seq && state_.base_seq <= state_.total_segments)
        {
            state_.dup_ack_count++;
            if (state_.dup_ack_count == 3 && !state_.in_fast_recovery)
            {
                std::cout << "[LOSS] Detected 3 duplicate ACKs for seq=" << state_.base_seq
                          << ", triggering fast retransmit (cwnd: " << state_.cwnd
                          << " -> " << (state_.cwnd / 2.0 + 3.0) << ")\n";
                state_.ssthresh = std::max(2.0, state_.cwnd / 2.0);
                state_.cwnd = state_.ssthresh + 3.0;
                state_.in_fast_recovery = true;
                fast_retx_needed = true;
            }
            else if (state_.in_fast_recovery)
            {
                state_.cwnd += 1.0;
            }
        }

        uint32_t mask = pkt.header.sack_mask;
        int gap_retx_count = 0;
        uint64_t now = now_ms();
        for (std::size_t i = 0; i < SACK_BITS; ++i)
        {
            if (mask & (1u << i))
            {
                mark_acked(ack + 1 + static_cast<uint32_t>(i));
            }
        }
        for (std::size_t i = 0; i < SACK_BITS; ++i)
        {
            uint32_t seq = ack + 1 + static_cast<uint32_t>(i);
            if (seq > state_.total_segments)
            {
                break;
            }
            auto &seg = segments_[seq - 1];
            if (seg.sent && !seg.acked)
            {
                if (mask & (1u << i))
                {
                    seg.sack_missing = false;
                }
                else if (!seg.sack_missing && gap_retx_count < MAX_SACK_RETX_PER_ACK &&
                         (now >= seg.last_send + MIN_GAP_RETX_INTERVAL_MS))
                {
                    seg.sack_missing = true;
                    ++gap_retx_count;
                    std::cout << "[RETRANSMIT] SACK gap seq=" << seq << "\n";
                    retransmit(seq, seg);
                }
            }
        }
        while (state_.base_seq <= state_.total_segments && segments_[state_.base_seq - 1].acked)
        {
            state_.base_seq++;
        }
    }

    bool ReliableSender::all_acked() const
    {
        for (const auto &s : segments_)
        {
            if (!s.acked)
            {
                return false;
            }
        }
        return true;
    }

    void ReliableSender::handle_timeouts()
    {
        uint64_t now = now_ms();
        for (uint32_t i = state_.base_seq; i <= state_.total_segments; ++i)
        {
            if (!segments_[i - 1].acked && segments_[i - 1].sent &&
                now - segments_[i - 1].last_send > DATA_TIMEOUT_MS)
            {
                timeout_count_++;
                std::cout << "[TIMEOUT] Packet seq=" << i << " timed out after "
                          << (now - segments_[i - 1].last_send) << "ms, retransmitting (cwnd: "
                          << state_.cwnd << " -> 1.0, ssthresh: " << state_.ssthresh
                          << " -> " << (state_.cwnd / 2.0) << ")\n";
                state_.ssthresh = std::max(2.0, state_.cwnd / 2.0);
                state_.cwnd = 1.0;
                state_.dup_ack_count = 0;
                state_.in_fast_recovery = false;
                retransmit(i, segments_[i - 1]);
            }
        }
    }

    void ReliableSender::try_send_data()
    {
        std::size_t window_cap = std::min<std::size_t>(window_size_, state_.peer_wnd ? state_.peer_wnd : window_size_);
        window_cap = std::min<std::size_t>(window_cap, static_cast<std::size_t>(std::floor(state_.cwnd)));
        window_cap = std::min<std::size_t>(window_cap, SACK_BITS);

        while (state_.next_seq <= state_.total_segments && state_.next_seq < state_.base_seq + window_cap)
        {
            auto &seg = segments_[state_.next_seq - 1];
            if (!seg.sent)
            {
                retransmit(state_.next_seq, seg);
                ++state_.next_seq;
            }
            else
            {
                break;
            }
        }
    }

    void ReliableSender::try_send_fin()
    {
        uint64_t now = now_ms();
        if (fin_complete_)
        {
            return;
        }
        if (!fin_sent_)
        {
            if (!all_acked())
            {
                return;
            }
            PacketHeader fin{};
            fin.seq = state_.total_segments + 1;
            fin.ack = 0;
            fin.flags = FLAG_FIN;
            fin.wnd = window_size_;
            fin.len = 0;
            fin.sack_mask = 0;
            send_raw(fin, {});
            fin_sent_ = true;
            fin_last_send_ = now;
            fin_retry_count_ = 0;
            std::cout << "[DEBUG] Sent FIN\n";
            return;
        }
        if (now - fin_last_send_ > HANDSHAKE_TIMEOUT_MS && fin_retry_count_ < MAX_FIN_RETRIES)
        {
            PacketHeader fin{};
            fin.seq = state_.total_segments + 1;
            fin.ack = 0;
            fin.flags = FLAG_FIN;
            fin.wnd = window_size_;
            fin.len = 0;
            fin.sack_mask = 0;
            send_raw(fin, {});
            fin_last_send_ = now;
            fin_retry_count_++;
            std::cout << "[DEBUG] Retrying FIN (attempt " << fin_retry_count_ << "/" << MAX_FIN_RETRIES << ")\n";
        }
    }

    void ReliableSender::process_network()
    {
        bool fast_retx_needed = false;
        Packet pkt{};
        sockaddr_in from{};
        if (wait_for_packet(pkt, from, 50))
        {
            if (!same_endpoint(from, remote_))
            {
                return;
            }
            if ((pkt.header.flags & FLAG_FIN) && (pkt.header.flags & FLAG_ACK))
            {
                PacketHeader final_ack{};
                final_ack.seq = pkt.header.ack;
                final_ack.ack = pkt.header.seq + 1;
                final_ack.flags = FLAG_ACK;
                final_ack.wnd = window_size_;
                final_ack.len = 0;
                final_ack.sack_mask = 0;
                send_raw(final_ack, {});
                fin_complete_ = true;
                std::cout << "[DEBUG] Received FIN+ACK, sent final ACK\n";
                return;
            }
            if (pkt.header.flags & FLAG_ACK)
            {
                handle_ack(pkt, fast_retx_needed);
                if (fast_retx_needed && state_.base_seq <= state_.total_segments)
                {
                    fast_retransmit_count_++;
                    std::cout << "[RETRANSMIT] Fast retransmit seq=" << state_.base_seq << "\n";
                    retransmit(state_.base_seq, segments_[state_.base_seq - 1]);
                }
            }
        }
    }

    int ReliableSender::run()
    {
        if (init_socket_lib() != 0)
        {
            return 1;
        }

        sock_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (!socket_valid(sock_))
        {
            std::cerr << "Failed to create socket\n";
            return 1;
        }

        // 
        if (local_port_ > 0)
        {
            sockaddr_in local_addr{};
            local_addr.sin_family = AF_INET;
            local_addr.sin_addr.s_addr = INADDR_ANY;
            local_addr.sin_port = htons(local_port_);
            if (bind(sock_, reinterpret_cast<const sockaddr *>(&local_addr), sizeof(local_addr)) < 0)
            {
                std::cerr << "Failed to bind to local port " << local_port_ << "\n";
                return 1;
            }
            std::cout << "[DEBUG] Bound to local port " << local_port_ << "\n";
        }
        else
        {
            std::cout << "[DEBUG] Using system-assigned port\n";
        }

        if (inet_pton(AF_INET, dest_ip_.c_str(), &remote_.sin_addr) <= 0)
        {
            std::cerr << "Invalid receiver address\n";
            return 1;
        }

        if (!handshake())
        {
            std::cerr << "Handshake failed\n";
            return 1;
        }

        std::ifstream in(file_path_, std::ios::binary);
        if (!in)
        {
            std::cerr << "Cannot open input file\n";
            return 1;
        }
        file_data_ = std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        uint32_t total_segments = static_cast<uint32_t>((file_data_.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD);
        std::cout << "[DEBUG] File size: " << file_data_.size() << " bytes, segments: " << total_segments << "\n";
        segments_.assign(total_segments, {});
        for (uint32_t i = 0; i < total_segments; ++i)
        {
            std::size_t start = i * MAX_PAYLOAD;
            std::size_t len = std::min<std::size_t>(MAX_PAYLOAD, file_data_.size() - start);
            segments_[i].data.insert(segments_[i].data.end(), file_data_.begin() + start,
                                     file_data_.begin() + start + len);
        }

        state_.total_segments = total_segments;
        state_.peer_wnd = state_.peer_wnd ? state_.peer_wnd : window_size_;
        state_.ssthresh = std::max<double>(2.0, state_.peer_wnd);

        std::cout << "[DEBUG] Starting transmission - Window: " << window_size_
                  << ", Initial cwnd: " << state_.cwnd
                  << ", ssthresh: " << state_.ssthresh << "\n";

        while (!fin_complete_)
        {
            try_send_data();
            process_network();
            handle_timeouts();
            if (!data_timing_recorded_ && all_acked())
            {
                if (start_time_ == 0)
                {
                    start_time_ = now_ms();
                }
                end_time_ = now_ms();
                data_timing_recorded_ = true;
            }
            try_send_fin();

            if (fin_sent_ && !fin_complete_ && fin_retry_count_ >= MAX_FIN_RETRIES)
            {
                std::cerr << "[WARN] FIN handshake failed after retries\n";
                break;
            }
        }

        if (!data_timing_recorded_)
        {
            if (start_time_ == 0)
            {
                start_time_ = now_ms();
            }
            end_time_ = now_ms();
        }
        double elapsed_s = (end_time_ > start_time_) ? (end_time_ - start_time_) / 1000.0 : 0.0;
        double throughput =
            (elapsed_s > 0) ? static_cast<double>(file_data_.size()) / elapsed_s / 1024.0 / 1024.0 : 0.0;
        std::cout << "[INFO] Transfer completed\n";
        std::cout << "[INFO] Final cwnd: " << state_.cwnd << ", Final ssthresh: " << state_.ssthresh << "\n";
        std::cout << "[STATS] Total retransmits: " << retransmit_count_
                  << " (Timeout: " << timeout_count_
                  << ", Fast retransmit: " << fast_retransmit_count_ << ")\n";
        std::cout << "[STATS] Packet loss rate: "
                  << (state_.total_segments > 0 ? (retransmit_count_ * 100.0 / state_.total_segments) : 0.0)
                  << "%\n";
        std::cout << "Sent " << file_data_.size() << " bytes in " << elapsed_s << " s, avg throughput " << throughput
                  << " MiB/s\n";

        if (!fin_complete_)
        {
            std::cerr << "[WARN] FIN handshake did not complete cleanly\n";
        }

        return fin_complete_ ? 0 : 1;
    }

} // namespace rtp
