// receiver.h
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
		ReliableReceiver(uint16_t listen_port, string output_path, uint16_t window_size = 32);
		~ReliableReceiver();

		int run();

	   private:
		// === 网络通信 ===

		/**
		 * 等待接收数据包
		 * @param pkt 输出参数，接收到的数据包
		 * @param from 输出参数，发送方地址
		 * @param timeout_ms 超时时间（毫秒），-1表示无限等待
		 * @return 是否成功接收到数据包
		 */
		bool wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms);

		/**
		 * 发送原始数据包
		 * @param hdr 包头
		 * @param payload 负载数据
		 * @return 发送的字节数
		 */
		int send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload);

		// === 连接管理 ===
		/**
		 * 执行三次握手（被动方）
		 * 流程：等待SYN -> 发送SYN+ACK -> 等待ACK
		 * 返回true表示握手成功
		 * 注：如果收到数据包，认为握手隐式完成
		 */
		bool do_handshake();

		/**
		 * 处理FIN包（连接关闭）
		 * @param fin_seq FIN包的序号
		 */
		void handle_fin(uint32_t fin_seq);

		// === ACK发送 ===
		void send_ack(bool fin = false, uint32_t fin_ack = 0);
		void send_rst();  // 发送RST段，强制终止连接

		// === 数据处理 ===
		void process_data_packet(const Packet& pkt, ofstream& out);

		// === Socket相关 ===
		socket_t sock_{INVALID_SOCKET_VALUE};  // 接收端Socket
		uint16_t listen_port_{0};			   // 监听端口
		string output_path_;				   // 输出文件路径
		uint16_t window_size_{0};			   // 接收窗口大小
		sockaddr_in client_{};				   // 客户端地址
		uint32_t isn_{0};					   // 本端初始序号
		uint32_t peer_isn_{0};				   // 对端初始序号

		// === 模块化组件 ===
		ReceiveBuffer buffer_;	// 接收缓冲区
		TransferStats stats_;	// 统计信息

		// === 统计 ===
		size_t bytes_written_{0};			  // 写入文件的字节数
		uint32_t total_packets_received_{0};  // 总接收包数
		uint32_t duplicate_packets_{0};		  // 重复包数
		uint32_t out_of_order_packets_{0};	  // 乱序包数

		// === 超时检测 ===
		int consecutive_timeouts_{0};  // 连续超时次数
	};

}  // namespace rtp
