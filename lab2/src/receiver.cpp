#include "receiver.h"

#include <fstream>
#include <iostream>

namespace rtp {
	using std::cerr;
	using std::cout;
	using std::endl;
	using std::ofstream;
	using std::size_t;
	using std::string;
	using std::vector;

	namespace {
		// 最大握手重试次数
		constexpr int MAX_HANDSHAKE_RETRIES = 5;
		// 最大FIN重试次数
		constexpr int MAX_FIN_RETRIES = 5;
		// SACK窗口大小限制（与SACK位图宽度一致）
		constexpr uint16_t SACK_WINDOW_LIMIT = 32;
		// 最大连续超时次数（超过则认为sender已断开）
		constexpr int MAX_CONSECUTIVE_TIMEOUTS = 10;

		// 检查两个地址是否相同
		bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b) {
			return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
		}
	}  // namespace

	// 窗口大小和SACK位图宽度保持一致
	ReliableReceiver::ReliableReceiver(uint16_t listen_port, string output_path, uint16_t window_size)
		: listen_port_(listen_port),
		  output_path_(std::move(output_path)),
		  window_size_(std::min<uint16_t>(window_size, SACK_WINDOW_LIMIT)),
		  buffer_(std::min<uint16_t>(window_size, SACK_WINDOW_LIMIT)) {}

	// 关闭Socket
	ReliableReceiver::~ReliableReceiver() {
		if (socket_valid(sock_)) {
			closesocket(sock_);
		}
	}

	bool ReliableReceiver::wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms) {
		fd_set rfds;		   // 声明读文件描述符集合
		FD_ZERO(&rfds);		   // 清空集合
		FD_SET(sock_, &rfds);  // 将socket加入集合
		timeval tv{};		   // 超时结构体
		if (timeout_ms >= 0) {
			// 设置超时时间
			tv.tv_sec = timeout_ms / 1000;			  // s
			tv.tv_usec = (timeout_ms % 1000) * 1000;  // ms
		}
		// 等待数据可读或超时
		// 它把 sock_ 交给内核监视可读事件，内核在有数据到达（或错误/关闭）时把 socket 标记为“可读”，select
		// 就返回 >0，rfds 里该 socket 仍在集合。然后才去 recvfrom 把包读出来；如果到超时时间还没有可读，select 返回
		// 0，视为超 时。
		int rv = select(0, &rfds, nullptr, nullptr, timeout_ms >= 0 ? &tv : nullptr);
		if (rv <= 0) {
			// 超时或出错
			return false;
		}

		// 接收数据包
		sockaddr_in peer{};
		socklen_t len = sizeof(peer);  // 地址长度
		vector<uint8_t> buf(2048);	   // 接收缓冲区
		// 接收数据
		int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
						 reinterpret_cast<sockaddr*>(&peer), &len);
		if (n <= 0) {
			// 接收失败
			return false;
		}
		if (!parse_packet(buf.data(), static_cast<size_t>(n), pkt)) {
			// 解析失败
			return false;
		}
		// 返回发送方地址
		from = peer;
		return true;
	}

	int ReliableReceiver::send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload) {
		// 序列化并发送数据包
		auto buffer = serialize_packet(hdr, payload);
		// 发送到客户端地址
		return sendto(sock_, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
					  reinterpret_cast<const sockaddr*>(&client_), sizeof(client_));
	}

	/**
	 * 发送ACK包
	 * @param fin 是否为FIN+ACK包
	 * @param fin_ack FIN的确认序号（仅在fin=true时有效）
	 * 普通ACK携带SACK掩码，通告发送端乱序段的到达情况
	 */
	void ReliableReceiver::send_ack(bool fin, uint32_t fin_ack) {
		PacketHeader ack{};
		ack.seq = isn_ + 1;	 // 本端下一个序号
		// ack 是否为 FIN+ACK，否则就是下一个序号
		ack.ack = fin ? fin_ack : buffer_.get_expected_seq();
		// 设置标志位、窗口大小、SACK掩码
		ack.flags = FLAG_ACK | (fin ? FLAG_FIN : 0);
		ack.wnd = window_size_;								  // 通告接收窗口大小
		ack.len = 0;										  // 普通ACK携带SACK掩码，FIN+ACK不携带
		ack.sack_mask = fin ? 0 : buffer_.build_sack_mask();  // SACK掩码
		// 发送ACK包
		send_raw(ack, {});
	}

	/**
	 * 发送RST段，强制终止连接
	 * 用于握手失败、校验错误、状态异常等场景
	 */
	void ReliableReceiver::send_rst() {
		PacketHeader rst{};
		rst.seq = isn_ + 1;
		rst.ack = peer_isn_ + 1;
		rst.flags = FLAG_RST;
		rst.wnd = 0;
		rst.len = 0;
		rst.sack_mask = 0;
		send_raw(rst, {});
		cout << "[RST] Sent RST segment to reset connection" << endl;
	}

	/**
	 * 执行三次握手（被动方）
	 * 流程：等待SYN -> 发送SYN+ACK -> 等待ACK
	 * 返回true表示握手成功
	 * 注：如果收到数据包，认为握手隐式完成
	 */
	bool ReliableReceiver::do_handshake() {
		Packet pkt{};
		sockaddr_in from{};
		cout << "Waiting for SYN on port " << listen_port_ << "..." << endl;
		while (true) {
			// 等待SYN包
			if (!wait_for_packet(pkt, from, -1)) {
				continue;
			}
			// 检查是否为SYN包
			if (!(pkt.header.flags & FLAG_SYN)) {
				continue;
			}

			// 收到SYN，记录客户端地址和ISN
			client_ = from;
			peer_isn_ = pkt.header.seq;
			cout << "[DEBUG] Received SYN from " << addr_to_string(from) << endl;

			// 生成本端ISN
			sockaddr_in local_info{};  // 本地地址信息
			int local_len = sizeof(local_info);
			if (getsockname(sock_, reinterpret_cast<sockaddr*>(&local_info), &local_len) != 0) {
				// 获取本地地址失败，使用通配地址
				local_info.sin_family = AF_INET;				 // IPv4
				local_info.sin_addr.s_addr = htonl(INADDR_ANY);	 // 任意地址
				local_info.sin_port = htons(listen_port_);		 // 监听端口
			}
			// 生成ISN
			isn_ = generate_isn(local_info, client_);

			// 发送SYN+ACK包
			PacketHeader syn_ack{};
			syn_ack.seq = isn_;					  // 本端ISN
			syn_ack.ack = peer_isn_ + 1;		  // 确认号为对端ISN+1
			syn_ack.flags = FLAG_SYN | FLAG_ACK;  // SYN和ACK标志
			syn_ack.wnd = window_size_;			  // 通告接收窗口大小
			syn_ack.len = 0;					  // 无负载
			syn_ack.sack_mask = 0;				  // 无SACK掩码

			bool acked = false;	 // 标记是否收到ACK
			// 等待ACK包
			for (int attempt = 0; attempt < MAX_HANDSHAKE_RETRIES && !acked; ++attempt) {
				send_raw(syn_ack, {});	// 发送SYN+ACK
				// 等待ACK或数据包（隐式完成握手）
				cout << "[DEBUG] Sent SYN+ACK (attempt " << (attempt + 1) << "/" << MAX_HANDSHAKE_RETRIES << ")"
					 << endl;

				// 等待 ACK 包
				Packet confirm{};
				sockaddr_in confirm_from{};
				if (wait_for_packet(confirm, confirm_from, HANDSHAKE_TIMEOUT_MS) &&
					same_endpoint(confirm_from, client_)) {
					// 收到RST段，连接被对方重置
					if (confirm.header.flags & FLAG_RST) {
						cerr << "[RST] Received RST during handshake, "
								"connection reset by peer"
							 << endl;
						return false;
					}
					// 收到 ACK 或数据包都表示握手完成
					if ((confirm.header.flags & FLAG_ACK) && confirm.header.ack == syn_ack.seq + 1) {
						cout << "[DEBUG] Received ACK, handshake completed" << endl;
						acked = true;
					} else if (confirm.header.flags & FLAG_DATA) {
						// 隐式完成握手
						cout << "[DEBUG] Received DATA (implicit ACK), "
								"handshake completed"
							 << endl;
						acked = true;
					}
				}
			}

			if (acked) {
				// 握手完成，初始化期望序列号为 peer_isn + 1
				buffer_.set_expected_seq(peer_isn_ + 1);
				return true;
			}

			cout << "[WARN] Handshake ACK not received, waiting for new SYN" << endl;
			send_rst();	 // 握手失败，发送RST终止连接
		}
		return false;
	}

	/**
	 * 处理接收到的数据包
	 * 1. 检查是否为重复包或窗口外的包
	 * 2. 将新包加入缓冲区
	 * 3. 提取连续段并写入文件
	 * 4. 发送ACK+SACK
	 */
	void ReliableReceiver::process_data_packet(const Packet& pkt, ofstream& out) {
		total_packets_received_++;		// 统计总接收包数
		uint32_t seq = pkt.header.seq;	// 数据包序号

		// 检查重复或窗口外
		if (seq < buffer_.get_expected_seq()) {
			// 收到重复包
			duplicate_packets_++;
			cout << "[DUP] Duplicate packet seq=" << seq << " (expected: " << buffer_.get_expected_seq() << ")" << endl;
			send_ack();	 // 发送ack
			return;
		}

		if (!buffer_.is_in_window(seq)) {
			// 超出接收窗口
			cout << "[OVERFLOW] Packet seq=" << seq << " out of window (expected: " << buffer_.get_expected_seq()
				 << ", window: " << window_size_ << ")" << endl;
			send_ack();	 // 发送ack
			return;
		}

		// 添加到缓冲区
		if (buffer_.add_segment(seq, pkt.payload)) {
			// 新包成功添加
			if (seq > buffer_.get_expected_seq()) {
				// 如果不是期望的序号，统计乱序包
				out_of_order_packets_++;
				cout << "[OOO] Out-of-order packet seq=" << seq << " (expected: " << buffer_.get_expected_seq() << ")"
					 << endl;
			}
		} else {
			// 重复包
			duplicate_packets_++;
			cout << "[DUP] Duplicate packet seq=" << seq << " (already buffered)" << endl;
		}

		// 提取并写入连续段
		auto segments = buffer_.extract_continuous_segments();
		for (const auto& data : segments) {
			out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
			bytes_written_ += data.size();
		}

		send_ack();	 // 发送ACK+SACK
	}

	/**
	 * 处理FIN包（连接关闭）
	 * 1. 发送FIN+ACK
	 * 2. 等待最终ACK
	 * 3. 如果超时未收到ACK，重传FIN+ACK（最多MAX_FIN_RETRIES次）
	 */

	void ReliableReceiver::handle_fin(uint32_t fin_seq) {
		cout << "[DEBUG] Received FIN" << endl;

		if (stats_.get_end_time() == 0) {
			// 记录结束时间
			stats_.set_end_time(now_ms());
		}

		// 发送FIN+ACK
		uint32_t fin_ack_seq = fin_seq + 1;
		send_ack(true, fin_ack_seq);
		cout << "[DEBUG] Sent FIN+ACK" << endl;

		// 等待最终ACK
		bool final_ack_seen = false;
		int fin_ack_attempts = 0;
		Packet final_ack{};
		sockaddr_in from{};

		while (fin_ack_attempts < MAX_FIN_RETRIES && !final_ack_seen) {
			// 当前次数小于最大重试次数且未收到最终ACK
			if (wait_for_packet(final_ack, from, HANDSHAKE_TIMEOUT_MS)) {
				// 收到数据包，检查是否为最终ACK
				if (!same_endpoint(from, client_)) {
					continue;
				}
				if (final_ack.header.flags & FLAG_ACK) {
					// 收到最终ACK，握手完成
					final_ack_seen = true;
					cout << "[DEBUG] Final ACK received, close handshake "
							"completed"
						 << endl;
				} else if (final_ack.header.flags & FLAG_FIN) {
					// 收到重复FIN，重传FIN+ACK
					send_ack(true, fin_ack_seq);
					cout << "[DEBUG] Re-sent FIN+ACK on duplicate FIN" << endl;
				}
			} else {
				// 超时未收到ACK，重传FIN+ACK
				++fin_ack_attempts;
				cout << "[DEBUG] Retrying FIN+ACK (attempt " << fin_ack_attempts << "/" << MAX_FIN_RETRIES << ")"
					 << endl;
				send_ack(true, fin_ack_seq);
			}
		}

		// 未收到最终ACK，失败
		if (!final_ack_seen) {
			cout << "[WARN] FIN handshake incomplete after retries" << endl;
		}
	}

	int ReliableReceiver::run() {
		WSADATA wsa;  // WSA 数据结构
		// 2.2 版本
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			cerr << "WSAStartup failed" << endl;
			return -1;
		}

		sock_ = socket(AF_INET, SOCK_DGRAM, 0);
		if (!socket_valid(sock_)) {
			cerr << "Failed to create socket" << endl;
			return 1;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(listen_port_);
		if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
			cerr << "Bind failed" << endl;
			return 1;
		}

		if (!do_handshake()) {
			cerr << "Handshake failed" << endl;
			return 1;
		}
		cout << "Connection established with " << addr_to_string(client_) << endl;
		ofstream out(output_path_, std::ios::binary);
		if (!out) {
			cerr << "Cannot open output file" << endl;
			return 1;
		}

		cout << "[DEBUG] Starting data reception - Window size: " << window_size_ << endl;
		stats_.set_start_time(now_ms());

		while (true) {
			Packet p{};
			sockaddr_in from{};
			if (!wait_for_packet(p, from, DATA_TIMEOUT_MS)) {
				// 连续超时检测
				consecutive_timeouts_++;
				if (consecutive_timeouts_ >= MAX_CONSECUTIVE_TIMEOUTS) {
					cerr << "[TIMEOUT] No data received for " << consecutive_timeouts_
						 << " consecutive timeouts (total " << (consecutive_timeouts_ * DATA_TIMEOUT_MS / 1000)
						 << "s), sender likely disconnected" << endl;
					break;
				}
				continue;
			}
			consecutive_timeouts_ = 0;	// 收到包后重置计数

			if (!same_endpoint(from, client_)) {
				continue;
			}

			// 处理RST段（sender强制断开）
			if (p.header.flags & FLAG_RST) {
				cerr << "[RST] Received RST from sender, connection reset" << endl;
				break;
			}

			if (p.header.flags & FLAG_FIN) {
				handle_fin(p.header.seq);
				break;
			}

			if (p.header.flags & FLAG_DATA) {
				process_data_packet(p, out);
			}
		}

		if (stats_.get_end_time() == 0) {
			stats_.set_end_time(now_ms());
		}
		if (stats_.get_start_time() == 0) {
			stats_.set_start_time(stats_.get_end_time());
		}

		stats_.print_receiver_stats(bytes_written_, total_packets_received_, out_of_order_packets_, duplicate_packets_);

		return 0;
	}

}  // namespace rtp
