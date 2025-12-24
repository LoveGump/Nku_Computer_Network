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
		/**
		 * @param dest_ip 目标IP地址
		 * @param dest_port 目标端口
		 * @param file_path 待发送文件路径
		 * @param window_size 发送窗口大小（最大SACK位图宽度）
		 * @param local_port 本地绑定端口（0表示随机端口）
		 */
		ReliableSender(string dest_ip, uint16_t dest_port, string file_path, uint16_t window_size,
					   uint16_t local_port = 0);
		~ReliableSender();

		int run();

	   private:
		bool wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms);
		int send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload);
		void send_rst();  // 发送RST段，强制终止连接

		bool handshake();		// 执行三次握手建立连接
		void try_send_fin();	// 尝试发送FIN段
		void handle_fin_ack();	// 处理收到的FIN确认
		void report_progress(bool force = false);
		void add_acked_bytes(uint32_t seq);

		void transmit_segment(uint32_t seq);
		void try_send_data();
		void process_network();

		//   处理ACK相关
		void handle_ack(const Packet& pkt);
		// 处理新ACK，推进窗口
		void handle_new_ack(uint32_t ack);
		// 处理重复ACK，可能触发快速重传
		void handle_duplicate_ack(uint32_t ack);
		// 处理SACK掩码，重传缺失段
		void handle_sack(uint32_t ack, uint32_t mask);

		// === 重传处理 ===
		void handle_timeouts();
		void fast_retransmit();
		void update_rto(uint64_t rtt_sample);  // 更新RTO（Jacobson/Karels算法）

		// === 窗口探测 ===
		void handle_window_probe();	 // 处理窗口探测逻辑
		void send_window_probe();	 // 发送窗口探测段

		// === Socket相关 ===
		socket_t sock_{INVALID_SOCKET_VALUE};  // 本端Socket
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
		vector<uint8_t> file_data_;	 // 待发送文件数据

		// === 模块化组件 ===
		SendWindow window_;				   // 发送窗口管理
		CongestionControl congestion_;	   // 拥塞控制
		TransferStats stats_;			   // 统计信息
		size_t bytes_acked_{0};			   // 已确认字节数（用于进度显示）
		uint64_t last_progress_print_{0};  // 上次进度打印时间
		int last_progress_percent_{-1};	   // 上次打印的进度百分比

		// === FIN状态 ===
		bool fin_sent_{false};		 // 是否已发送FIN
		bool fin_complete_{false};	 // FIN是否完成
		uint64_t fin_last_send_{0};	 // 上次发送FIN的时间
		int fin_retry_count_{0};	 // FIN重传计数
		bool data_timing_recorded_{false};

		// === 超时检测 ===
		uint64_t last_ack_time_{0};	 // 最后收到ACK的时间

		// === 窗口探测（Window Probing / Persist Timer） ===
		bool zero_window_{false};	 // 处于零窗口状态
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
