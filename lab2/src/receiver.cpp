#include "receiver.h"

#include <fstream>
#include <iostream>

namespace rtp
{
    namespace
    {
        constexpr int MAX_HANDSHAKE_RETRIES = 5;
        constexpr int MAX_FIN_RETRIES = 5;

        bool same_endpoint(const sockaddr_in &a, const sockaddr_in &b)
        {
            return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
        }
    } // namespace

    ReliableReceiver::ReliableReceiver(uint16_t listen_port, std::string output_path, uint16_t window_size)
        : listen_port_(listen_port), output_path_(std::move(output_path)), window_size_(window_size) {}

    ReliableReceiver::~ReliableReceiver()
    {
        if (socket_valid(sock_))
        {
            close_socket(sock_);
        }
    }

    bool ReliableReceiver::wait_for_packet(Packet &pkt, sockaddr_in &from, int timeout_ms)
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

    int ReliableReceiver::send_raw(const PacketHeader &hdr, const std::vector<uint8_t> &payload)
    {
        auto buffer = serialize_packet(hdr, payload);
        return sendto(sock_, reinterpret_cast<const char *>(buffer.data()), static_cast<int>(buffer.size()), 0,
                      reinterpret_cast<const sockaddr *>(&client_), sizeof(client_));
    }

    uint32_t ReliableReceiver::build_sack_mask() const
    {
        uint32_t mask = 0;
        for (uint32_t i = 0; i < 32; ++i)
        {
            uint32_t seq = state_.expected_seq + 1 + i;
            if (state_.buffer.find(seq) != state_.buffer.end())
            {
                mask |= (1u << i);
            }
        }
        return mask;
    }

    void ReliableReceiver::send_ack(bool fin, uint32_t fin_ack)
    {
        PacketHeader ack{};
        ack.seq = 0;
        ack.ack = fin ? fin_ack : state_.expected_seq;
        ack.flags = FLAG_ACK | (fin ? FLAG_FIN : 0);
        ack.wnd = window_size_;
        ack.len = 0;
        ack.sack_mask = fin ? 0 : build_sack_mask();
        send_raw(ack, {});
    }

    bool ReliableReceiver::do_handshake()
    {
        Packet pkt{};
        sockaddr_in from{};
        std::cout << "Waiting for SYN on port " << listen_port_ << "...\n";
        while (true)
        {
            if (!wait_for_packet(pkt, from, -1))
            {
                continue;
            }
            if (!(pkt.header.flags & FLAG_SYN))
            {
                continue;
            }

            client_ = from;
            uint32_t syn_seq = pkt.header.seq;
            std::cout << "[DEBUG] Received SYN from " << addr_to_string(from) << "\n";

            PacketHeader syn_ack{};
            syn_ack.seq = 0;
            syn_ack.ack = syn_seq + 1;
            syn_ack.flags = FLAG_SYN | FLAG_ACK;
            syn_ack.wnd = window_size_;
            syn_ack.len = 0;
            syn_ack.sack_mask = 0;

            bool acked = false;
            for (int attempt = 0; attempt < MAX_HANDSHAKE_RETRIES && !acked; ++attempt)
            {
                send_raw(syn_ack, {});
                std::cout << "[DEBUG] Sent SYN+ACK (attempt " << (attempt + 1) << "/" << MAX_HANDSHAKE_RETRIES << ")\n";

                Packet confirm{};
                sockaddr_in confirm_from{};
                if (wait_for_packet(confirm, confirm_from, HANDSHAKE_TIMEOUT_MS) &&
                    same_endpoint(confirm_from, client_) && (confirm.header.flags & FLAG_ACK) &&
                    confirm.header.ack == syn_ack.seq + 1)
                {
                    std::cout << "[DEBUG] Received ACK, handshake completed\n";
                    acked = true;
                }
            }

            if (acked)
            {
                return true;
            }

            std::cout << "[WARN] Handshake ACK not received, waiting for new SYN\n";
        }
        return false;
    }

    int ReliableReceiver::run()
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
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(listen_port_);
        if (bind(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            std::cerr << "Bind failed\n";
            return 1;
        }

        if (!do_handshake())
        {
            std::cerr << "Handshake failed\n";
            return 1;
        }
        std::cout << "Connection established with " << addr_to_string(client_) << "\n";

        std::ofstream out(output_path_, std::ios::binary);
        if (!out)
        {
            std::cerr << "Cannot open output file\n";
            return 1;
        }

        constexpr uint16_t SACK_WINDOW_LIMIT = 32;
        state_.window_size = std::min<uint16_t>(window_size_, SACK_WINDOW_LIMIT);
        window_size_ = state_.window_size;
        std::cout << "[DEBUG] Starting data reception - Window size: " << window_size_ << "\n";
        start_time_ = now_ms();
        bool fin_seen = false;
        uint32_t fin_ack_seq = 0;
        bool final_ack_seen = false;
        int fin_ack_attempts = 0;

        while (true)
        {
            Packet p{};
            sockaddr_in from{};
            if (!wait_for_packet(p, from, DATA_TIMEOUT_MS))
            {
                continue;
            }
            if (!same_endpoint(from, client_))
            {
                continue;
            }
            if (p.header.flags & FLAG_FIN)
            {
                std::cout << "[DEBUG] Received FIN\n";
                fin_seen = true;
                fin_ack_seq = p.header.seq + 1;
                if (end_time_ == 0)
                {
                    end_time_ = now_ms();
                }
                send_ack(true, fin_ack_seq);
                std::cout << "[DEBUG] Sent FIN+ACK\n";
                break;
            }
            if (!(p.header.flags & FLAG_DATA))
            {
                continue;
            }
            total_packets_received_++;
            uint32_t seq = p.header.seq;
            if (seq < state_.expected_seq)
            {
                duplicate_packets_++;
                std::cout << "[DUP] Duplicate packet seq=" << seq << " (expected: " << state_.expected_seq << ")\n";
                send_ack();
                continue;
            }
            if (seq >= state_.expected_seq + window_size_)
            {
                std::cout << "[OVERFLOW] Packet seq=" << seq << " out of window (expected: "
                          << state_.expected_seq << ", window: " << window_size_ << ")\n";
                send_ack();
                continue;
            }

            if (state_.buffer.find(seq) == state_.buffer.end())
            {
                if (seq > state_.expected_seq)
                {
                    out_of_order_packets_++;
                    std::cout << "[OOO] Out-of-order packet seq=" << seq << " (expected: " << state_.expected_seq << ")\n";
                }
                state_.buffer.emplace(seq, p.payload);
            }
            else
            {
                duplicate_packets_++;
                std::cout << "[DUP] Duplicate packet seq=" << seq << " (already buffered)\n";
            }

            while (true)
            {
                auto it = state_.buffer.find(state_.expected_seq);
                if (it == state_.buffer.end())
                {
                    break;
                }
                out.write(reinterpret_cast<const char *>(it->second.data()), static_cast<std::streamsize>(it->second.size()));
                bytes_written_ += it->second.size();
                state_.buffer.erase(it);
                state_.expected_seq++;
            }
            send_ack();
        }

        if (end_time_ == 0)
        {
            end_time_ = now_ms();
        }
        if (start_time_ == 0)
        {
            start_time_ = end_time_;
        }
        double elapsed_s = (end_time_ > start_time_) ? (end_time_ - start_time_) / 1000.0 : 0.0;
        double throughput =
            (elapsed_s > 0) ? static_cast<double>(bytes_written_) / elapsed_s / 1024.0 / 1024.0 : 0.0;
        std::cout << "[INFO] Transfer completed\n";
        std::cout << "[STATS] Total packets received: " << total_packets_received_ << "\n";
        std::cout << "[STATS] Out-of-order packets: " << out_of_order_packets_ << "\n";
        std::cout << "[STATS] Duplicate packets: " << duplicate_packets_ << "\n";
        std::cout << "Received " << bytes_written_ << " bytes in " << elapsed_s << " s, avg throughput " << throughput
                  << " MiB/s\n";

        if (fin_seen)
        {
            Packet final_ack{};
            sockaddr_in from{};
            while (fin_ack_attempts < MAX_FIN_RETRIES && !final_ack_seen)
            {
                if (wait_for_packet(final_ack, from, HANDSHAKE_TIMEOUT_MS))
                {
                    if (!same_endpoint(from, client_))
                    {
                        continue;
                    }
                    if (final_ack.header.flags & FLAG_ACK)
                    {
                        final_ack_seen = true;
                        std::cout << "[DEBUG] Final ACK received, close handshake completed\n";
                    }
                    else if (final_ack.header.flags & FLAG_FIN)
                    {
                        send_ack(true, fin_ack_seq);
                        std::cout << "[DEBUG] Re-sent FIN+ACK on duplicate FIN\n";
                    }
                }
                else
                {
                    ++fin_ack_attempts;
                    std::cout << "[DEBUG] Retrying FIN+ACK (attempt " << fin_ack_attempts << "/" << MAX_FIN_RETRIES << ")\n";
                    send_ack(true, fin_ack_seq);
                }
            }
            if (!final_ack_seen)
            {
                std::cout << "[WARN] FIN handshake incomplete after retries\n";
            }
        }

        return 0;
    }

} // namespace rtp
