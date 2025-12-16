#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "receive_buffer.h"
#include "rtp.h"
#include "transfer_stats.h"

namespace rtp {
	using std::ofstream;
	using std::size_t;
	using std::string;
	using std::vector;

	// 可靠接收端
	class ReliableReceiver {
	   public:
		ReliableReceiver(uint16_t listen_port, string output_path,
						 uint16_t window_size);
		~ReliableReceiver();

		int run();

	   private:
		// === 网络通信 ===
		bool wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms);
		int send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload);

		// === 连接管理 ===
		bool do_handshake();
		void handle_fin(uint32_t fin_seq);

		// === ACK发送 ===
		void send_ack(bool fin = false, uint32_t fin_ack = 0);
		void send_rst();  // 发送RST段，强制终止连接

		// === 数据处理 ===
		void process_data_packet(const Packet& pkt, ofstream& out);

		// === Socket相关 ===
		socket_t sock_{INVALID_SOCKET_VALUE};
		uint16_t listen_port_{0};
		string output_path_;
		uint16_t window_size_{0};
		sockaddr_in client_{};
		uint32_t isn_{0};
		uint32_t peer_isn_{0};

		// === 模块化组件 ===
		ReceiveBuffer buffer_;	// 接收缓冲区
		TransferStats stats_;	// 统计信息

		// === 统计 ===
		size_t bytes_written_{0};
		uint32_t total_packets_received_{0};
		uint32_t duplicate_packets_{0};
		uint32_t out_of_order_packets_{0};

		// === 超时检测 ===
		int consecutive_timeouts_{0};  // 连续超时次数
	};

}  // namespace rtp
