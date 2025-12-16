#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "congestion_control.h"
#include "rtp.h"
#include "send_window.h"
#include "transfer_stats.h"

namespace rtp {
	using std::string;
	using std::vector;
	// 可靠发送端
	class ReliableSender {
	   public:
		ReliableSender(string dest_ip, uint16_t dest_port, string file_path,
					   uint16_t window_size, uint16_t local_port = 0);
		~ReliableSender();

		int run();

	   private:
		// === 网络通信 ===
		bool wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms);
		int send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload);
		void send_rst();  // 发送RST段，强制终止连接

		// === 连接管理 ===
		bool handshake();
		void try_send_fin();
		void handle_fin_ack();

		// === 数据传输 ===
		void transmit_segment(uint32_t seq);
		void try_send_data();
		void process_network();

		// === ACK处理 ===
		void handle_ack(const Packet& pkt);
		void handle_new_ack(uint32_t ack);
		void handle_duplicate_ack(uint32_t ack);
		void handle_sack(uint32_t ack, uint32_t mask);

		// === 重传处理 ===
		void handle_timeouts();
		void fast_retransmit();
		void update_rto(uint64_t rtt_sample);  // 更新RTO（Jacobson/Karels算法）

		// === 窗口探测 ===
		void handle_window_probe();	 // 处理窗口探测逻辑
		void send_window_probe();	 // 发送窗口探测段

		// === Socket相关 ===
		socket_t sock_{INVALID_SOCKET_VALUE};
		sockaddr_in remote_{};
		string dest_ip_;
		uint16_t dest_port_{0};
		uint16_t local_port_{0};
		uint32_t isn_{0};
		uint32_t peer_isn_{0};

		// === 文件与配置 ===
		string file_path_;
		uint16_t window_size_{0};
		uint16_t peer_wnd_{0};
		vector<uint8_t> file_data_;

		// === 模块化组件 ===
		SendWindow window_;				// 发送窗口管理
		CongestionControl congestion_;	// 拥塞控制
		TransferStats stats_;			// 统计信息

		// === FIN状态 ===
		bool fin_sent_{false};
		bool fin_complete_{false};
		uint64_t fin_last_send_{0};
		int fin_retry_count_{0};
		bool data_timing_recorded_{false};

		// === 超时检测 ===
		uint64_t last_ack_time_{0};	 // 最后收到ACK的时间

		// === 窗口探测（Window Probing / Persist Timer） ===
		bool zero_window_{false};	 // 对端窗口是否为零
		uint64_t persist_timer_{0};	 // 持续计时器（下次探测时间）
		int persist_backoff_{0};	 // 指数退避级别（0~12）
		uint32_t probe_seq_{0};		 // 窗口探测序列号

		// === RTO自适应（Jacobson/Karels算法） ===
		double srtt_{0.0};			   // 平滑RTT（毫秒）
		double rttvar_{0.0};		   // RTT方差（毫秒）
		int rto_{1000};				   // 当前RTO（毫秒），初始1秒
		bool rtt_initialized_{false};  // 是否已初始化RTT
	};

}  // namespace rtp
