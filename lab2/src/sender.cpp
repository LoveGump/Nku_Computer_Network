#include "sender.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace rtp {
	using std::cerr;
	using std::cout;
	using std::ifstream;
	using std::size_t;
	using std::string;
	using std::vector;

	namespace {
		// SACK位图宽度（支持标记32个段）
		constexpr size_t SACK_BITS = 32;
		// 最大握手重试次数
		constexpr int MAX_HANDSHAKE_RETRIES = 5;
		// 最大FIN重试次数
		constexpr int MAX_FIN_RETRIES = 5;
		// 单次ACK最多重传的SACK缺口段数
		constexpr int MAX_SACK_RETX_PER_ACK = 4;
		// SACK缺口重传最小间隔（避免频繁重传）
		constexpr int MIN_GAP_RETX_INTERVAL_MS = DATA_TIMEOUT_MS / 2;
		// 单个数据段最大重传次数（超过则认为连接断开）
		constexpr int MAX_RETRANSMITS = 15;
		// 全局无响应超时（30秒）
		constexpr int GLOBAL_TIMEOUT_MS = 30000;

		// 检查两个地址是否相同
		bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b) {
			return a.sin_addr.s_addr == b.sin_addr.s_addr &&
				   a.sin_port == b.sin_port;
		}
	}  // namespace

	ReliableSender::ReliableSender(string dest_ip, uint16_t dest_port,
								   string file_path, uint16_t window_size,
								   uint16_t local_port)
		: dest_ip_(std::move(dest_ip)),
		  dest_port_(dest_port),
		  local_port_(local_port),
		  file_path_(std::move(file_path)),
		  window_size_(std::min<uint16_t>(window_size,
										  static_cast<uint16_t>(SACK_BITS))),
		  peer_wnd_(0),
		  isn_(0),
		  peer_isn_(0) {
		remote_.sin_family = AF_INET;
		remote_.sin_port = htons(dest_port_);
	}

	ReliableSender::~ReliableSender() {
		if (socket_valid(sock_)) {
			closesocket(sock_);
		}
	}

	bool ReliableSender::wait_for_packet(Packet& pkt, sockaddr_in& from,
										 int timeout_ms) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(sock_, &rfds);
		timeval tv{};
		if (timeout_ms >= 0) {
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
		}
		int nfds = 0;
		int rv = select(nfds, &rfds, nullptr, nullptr,
						timeout_ms >= 0 ? &tv : nullptr);
		if (rv <= 0) {
			return false;
		}
		sockaddr_in peer{};
		socklen_t len = sizeof(peer);
		vector<uint8_t> buf(2048);
		int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()),
						 static_cast<int>(buf.size()), 0,
						 reinterpret_cast<sockaddr*>(&peer), &len);
		if (n <= 0) {
			return false;
		}
		if (!parse_packet(buf.data(), static_cast<size_t>(n), pkt)) {
			return false;
		}
		from = peer;
		return true;
	}

	int ReliableSender::send_raw(const PacketHeader& hdr,
								 const vector<uint8_t>& payload) {
		auto buffer = serialize_packet(hdr, payload);
		return sendto(sock_, reinterpret_cast<const char*>(buffer.data()),
					  static_cast<int>(buffer.size()), 0,
					  reinterpret_cast<const sockaddr*>(&remote_),
					  sizeof(remote_));
	}

	/**
	 * 发送RST段，强制终止连接
	 * 用于握手失败、校验错误、状态异常等场景
	 */
	void ReliableSender::send_rst() {
		PacketHeader rst{};
		rst.seq = isn_ + 1;
		rst.ack = peer_isn_ + 1;
		rst.flags = FLAG_RST;
		rst.wnd = 0;
		rst.len = 0;
		rst.sack_mask = 0;
		send_raw(rst, {});
		cout << "[RST] Sent RST segment to reset connection\n";
	}

	/**
	 * 执行三次握手建立连接
	 * 流程：SYN -> SYN+ACK -> ACK
	 * 返回true表示握手成功
	 */
	bool ReliableSender::handshake() {
		PacketHeader syn{};
		syn.seq = isn_;
		syn.ack = 0;
		syn.wnd = window_size_;
		syn.len = 0;
		syn.flags = FLAG_SYN;
		syn.sack_mask = 0;

		cout << "[DEBUG] Starting handshake with " << dest_ip_ << ":"
			 << dest_port_ << "\n";
		for (int attempt = 0; attempt < MAX_HANDSHAKE_RETRIES; ++attempt) {
			cout << "[DEBUG] Sending SYN (attempt " << (attempt + 1) << "/"
				 << MAX_HANDSHAKE_RETRIES << ")\n";
			send_raw(syn, {});
			Packet pkt{};
			sockaddr_in from{};
			if (!wait_for_packet(pkt, from, HANDSHAKE_TIMEOUT_MS)) {
				continue;
			}
			if (!same_endpoint(from, remote_)) {
				cout << "[DEBUG] Ignoring handshake response from unexpected "
						"peer\n";
				continue;
			}
			// 收到RST段，连接被对方重置
			if (pkt.header.flags & FLAG_RST) {
				cerr << "[RST] Received RST during handshake, connection reset "
						"by peer\n";
				return false;
			}
			if ((pkt.header.flags & FLAG_SYN) &&
				(pkt.header.flags & FLAG_ACK) &&
				pkt.header.ack == syn.seq + 1) {
				peer_isn_ = pkt.header.seq;
				peer_wnd_ = std::min<uint16_t>(
					pkt.header.wnd, static_cast<uint16_t>(SACK_BITS));
				cout << "[DEBUG] Received SYN+ACK, peer window size: "
					 << peer_wnd_ << "\n";
				PacketHeader ack{};
				ack.seq = isn_ + 1;
				ack.ack = peer_isn_ + 1;
				ack.flags = FLAG_ACK;
				ack.wnd = window_size_;
				ack.len = 0;
				ack.sack_mask = 0;
				send_raw(ack, {});
				cout << "[DEBUG] Handshake completed successfully\n";
				return true;
			}
		}
		cerr << "[WARN] Handshake failed after retries\n";
		send_rst();	 // 握手失败，发送RST终止连接
		return false;
	}

	/**
	 * 发送或重传指定序号的数据段
	 * seq: 段序号（相对序号，从1开始）
	 * 如果是重传，会记录统计信息
	 */
	void ReliableSender::transmit_segment(uint32_t seq) {
		auto& seg = window_.get_segment(seq);
		bool is_retransmit = seg.sent;

		// 检查重传次数，超过限制认为连接失败
		if (is_retransmit) {
			seg.retrans_count++;
			seg.is_retransmitted = true;  // 标记为重传（Karn算法）
			if (seg.retrans_count > MAX_RETRANSMITS) {
				cerr << "[ERROR] Segment " << seq
					 << " exceeded max retransmits (" << MAX_RETRANSMITS
					 << "), connection lost\n";
				send_rst();
				throw std::runtime_error(
					"Connection lost: max retransmits "
					"exceeded");
			}
		} else {
			// 首次发送，记录时间戳用于RTT测量
			seg.send_timestamp = now_ms();
			seg.is_retransmitted = false;
		}

		PacketHeader hdr{};
		hdr.seq = isn_ + seq;
		hdr.ack = 0;
		hdr.flags = FLAG_DATA;
		hdr.wnd = window_size_;
		hdr.len = static_cast<uint16_t>(seg.data.size());
		hdr.sack_mask = 0;

		if (stats_.get_start_time() == 0) {
			stats_.set_start_time(now_ms());
		}

		send_raw(hdr, seg.data);
		seg.sent = true;
		seg.last_send = now_ms();

		if (is_retransmit) {
			stats_.record_retransmit();
		}
	}

	/**
	 * 处理新ACK（推进窗口）
	 * ack: 新的累积确认序号
	 * 标记所有 < ack 的段为已确认，并调用拥塞控制
	 */
	void ReliableSender::handle_new_ack(uint32_t ack) {
		// 测量RTT（Karn算法：只测量未重传的段）
		for (uint32_t i = window_.get_base_seq();
			 i < ack && i <= window_.total_segments(); ++i) {
			auto& seg = window_.get_segment(i);
			if (!seg.acked && seg.sent && !seg.is_retransmitted &&
				seg.send_timestamp > 0) {
				uint64_t rtt_sample = now_ms() - seg.send_timestamp;
				update_rto(rtt_sample);
				break;	// 只测量一个RTT样本
			}
		}

		// 标记所有被确认的段
		for (uint32_t s = window_.get_base_seq();
			 s < ack && s <= window_.total_segments(); ++s) {
			window_.mark_acked(s);
		}

		window_.set_base_seq(ack);

		// NewReno: 检测部分ACK并处理
		bool is_partial_ack =
			congestion_.on_new_ack(ack, window_.get_next_seq());
		if (is_partial_ack) {
			// 部分ACK：重传下一个未确认的段
			uint32_t next_unacked = ack;
			if (next_unacked <= window_.total_segments()) {
				auto& seg = window_.get_segment(next_unacked);
				if (!seg.acked) {
					cout << "[NewReno] Retransmitting next unacked segment: "
						 << next_unacked << std::endl;
					transmit_segment(next_unacked);
				}
			}
		}
	}

	/**
	 * 更新RTO（Jacobson/Karels算法）
	 * rtt_sample: RTT样本（毫秒）
	 * 公式：
	 *   SRTT = (1-α) × SRTT + α × RTT  (α = 1/8)
	 *   RTTVAR = (1-β) × RTTVAR + β × |SRTT - RTT|  (β = 1/4)
	 *   RTO = SRTT + 4 × RTTVAR
	 */
	// ========================================
	// 窗口探测
	// ========================================

	/**
	 * 发送窗口探测段
	 * 当对端窗口为零时，发送空ACK探测对端是否恢复窗口
	 */
	void ReliableSender::send_window_probe() {
		PacketHeader hdr;
		hdr.seq = window_.get_next_seq();  // 使用当前窗口序列号
		hdr.ack = 0;
		hdr.wnd = window_size_;
		hdr.len = 0;		   // 空探测包
		hdr.flags = FLAG_ACK;  // 仅ACK标志
		hdr.sack_mask = 0;
		hdr.checksum = 0;

		vector<uint8_t> empty_payload;
		send_raw(hdr, empty_payload);
		probe_seq_ = hdr.seq;
		cout << "[探测] 发送窗口探测包 seq=" << hdr.seq
			 << " backoff=" << persist_backoff_ << std::endl;
	}

	/**
	 * 处理窗口探测逻辑
	 * 在zero_window_状态下，定期发送探测包，并指数退避
	 */
	void ReliableSender::handle_window_probe() {
		if (!zero_window_) {
			return;	 // 非零窗口，不需要探测
		}

		uint64_t now = now_ms();
		if (now >= persist_timer_) {
			// 持续计时器超时，发送探测包
			send_window_probe();

			// 指数退避：5s → 10s → 20s → 40s → 60s(max)
			persist_backoff_ = std::min(persist_backoff_ + 1, 12);
			int interval = std::min(5000 * (1 << persist_backoff_), 60000);
			persist_timer_ = now + interval;
		}
	}

	// ========================================
	// RTO自适应
	// ========================================

	/**
	 * 更新RTO（Jacobson/Karels算法）
	 * 使用RTT样本更新SRTT和RTTVAR，然后计算新的RTO
	 */
	void ReliableSender::update_rto(uint64_t rtt_sample) {
		constexpr double ALPHA = 0.125;	 // 1/8
		constexpr double BETA = 0.25;	 // 1/4
		constexpr int MIN_RTO = 200;	 // 最小RTO 200ms
		constexpr int MAX_RTO = 60000;	 // 最大RTO 60s

		if (!rtt_initialized_) {
			// 首次RTT样本：直接初始化
			srtt_ = static_cast<double>(rtt_sample);
			rttvar_ = srtt_ / 2.0;
			rtt_initialized_ = true;
		} else {
			// 后续样本：指数加权平均
			double delta = static_cast<double>(rtt_sample) - srtt_;
			srtt_ = (1.0 - ALPHA) * srtt_ + ALPHA * rtt_sample;
			rttvar_ = (1.0 - BETA) * rttvar_ + BETA * std::abs(delta);
		}

		// 计算RTO = SRTT + 4 × RTTVAR
		int new_rto = static_cast<int>(srtt_ + 4.0 * rttvar_);
		int old_rto = rto_;
		rto_ = std::max(MIN_RTO, std::min(new_rto, MAX_RTO));

		// 只在RTO值改变时打印日志
		if (rto_ != old_rto) {
			cout << "[RTO] RTT=" << rtt_sample
				 << "ms, SRTT=" << static_cast<int>(srtt_)
				 << "ms, RTTVAR=" << static_cast<int>(rttvar_)
				 << "ms, RTO=" << rto_ << "ms (changed from " << old_rto
				 << "ms)\n";
		}
	}

	/**
	 * 处理重复ACK
	 * 累积3个重复ACK后触发快速重传
	 */
	void ReliableSender::handle_duplicate_ack(uint32_t ack) {
		congestion_.on_duplicate_ack();

		if (congestion_.should_fast_retransmit()) {
			congestion_.on_fast_retransmit(window_.get_next_seq());
			fast_retransmit();
		}
	}

	/**
	 * 处理选择性确认（SACK）
	 * ack: 累积确认序号
	 * mask: SACK掩码，标记ack之后32个段的到达情况
	 * 根据掩码标记已到达的段，并重传缺口段
	 */
	void ReliableSender::handle_sack(uint32_t ack, uint32_t mask) {
		// 标记SACK确认的段
		for (size_t i = 0; i < SACK_BITS; ++i) {
			if (mask & (1u << i)) {
				window_.mark_acked(ack + 1 + static_cast<uint32_t>(i));
			}
		}

		// 处理SACK缺口重传（限速：单次ACK最多重传4个）
		int gap_retx_count = 0;
		uint64_t now = now_ms();
		for (size_t i = 0; i < SACK_BITS; ++i) {
			uint32_t seq = ack + 1 + static_cast<uint32_t>(i);
			if (seq > window_.total_segments()) {
				break;
			}
			auto& seg = window_.get_segment(seq);
			if (seg.sent && !seg.acked && !(mask & (1u << i))) {
				// 该段已发送但未确认，且不在SACK中 -> 缺口
				uint64_t last_gap =
					seg.last_sack_retx ? seg.last_sack_retx : seg.last_send;
				if (gap_retx_count < MAX_SACK_RETX_PER_ACK &&
					now >= last_gap + MIN_GAP_RETX_INTERVAL_MS) {
					// 限速重传：避免频繁重传同一个段
					seg.last_sack_retx = now;
					++gap_retx_count;
					cout << "[RETRANSMIT] SACK gap seq=" << seq << "\n";
					transmit_segment(seq);
				}
			}
		}
	}

	void ReliableSender::handle_ack(const Packet& pkt) {
		// 更新最后收到ACK的时间（用于全局超时检测）
		last_ack_time_ = now_ms();

		// 获取接收方窗口大小
		uint16_t new_peer_wnd = std::min<uint16_t>(
			pkt.header.wnd, static_cast<uint16_t>(SACK_BITS));

		// 检测零窗口状态，启动/停止持续计时器
		if (new_peer_wnd == 0 && !zero_window_) {
			// 进入零窗口状态
			zero_window_ = true;
			persist_backoff_ = 0;
			persist_timer_ = now_ms() + 5000;  // 首次探测：5秒
			cout << "[窗口] 对端窗口为零，启动持续计时器" << std::endl;
		} else if (new_peer_wnd > 0 && zero_window_) {
			// 离开零窗口状态
			zero_window_ = false;
			persist_backoff_ = 0;
			cout << "[窗口] 对端窗口恢复：" << new_peer_wnd << std::endl;
		}

		peer_wnd_ = new_peer_wnd;

		uint32_t ack_abs = pkt.header.ack;
		if (ack_abs <= isn_) {
			return;
		}
		uint32_t ack = ack_abs - isn_;
		if (ack > window_.get_base_seq()) {
			handle_new_ack(ack);
		} else if (ack == window_.get_base_seq() &&
				   window_.get_base_seq() <= window_.total_segments()) {
			handle_duplicate_ack(ack);
		}

		handle_sack(ack, pkt.header.sack_mask);
		window_.advance_base_seq();
	}

	/**
	 * 检查并处理超时的数据段
	 * 遍历窗口内所有已发送但未确认的段
	 * 如果超过DATA_TIMEOUT_MS，则触发超时重传和拥塞控制
	 */
	void ReliableSender::handle_timeouts() {
		uint64_t now = now_ms();
		for (uint32_t i = window_.get_base_seq(); i <= window_.total_segments();
			 ++i) {
			auto& seg = window_.get_segment(i);
			// 使用动态RTO代替固定超时值
			if (!seg.acked && seg.sent &&
				now - seg.last_send > static_cast<uint64_t>(rto_)) {
				stats_.record_timeout();
				cout << "[TIMEOUT] Packet seq=" << i << " timed out after "
					 << (now - seg.last_send) << "ms (RTO=" << rto_
					 << "ms), retransmitting\n";
				congestion_.on_timeout();
				// 超时后倍增RTO（Karn算法）
				rto_ = std::min(rto_ * 2, 60000);
				transmit_segment(i);
			}
		}
	}

	void ReliableSender::fast_retransmit() {
		if (window_.get_base_seq() <= window_.total_segments()) {
			stats_.record_fast_retransmit();
			cout << "[RETRANSMIT] Fast retransmit seq="
				 << window_.get_base_seq() << "\n";
			transmit_segment(window_.get_base_seq());
		}
	}

	/**
	 * 尝试发送新数据
	 * 根据窗口大小（本地、对端、拥塞窗口的最小值）发送数据
	 * 只发送未发送过的段，不处理重传
	 */
	void ReliableSender::try_send_data() {
		// 零窗口时由窗口探测机制处理，这里不发送常规数据
		if (peer_wnd_ == 0) {
			return;	 // 零窗口，等待窗口探测
		}

		// 计算实际窗口大小 = min(本地窗口, 对端窗口, floor(cwnd), SACK位宽)
		size_t window_cap = window_.calculate_window_size(
			window_size_, peer_wnd_, congestion_.get_cwnd(), SACK_BITS);

		while (window_.get_next_seq() <= window_.total_segments() &&
			   window_.get_next_seq() < window_.get_base_seq() + window_cap) {
			auto& seg = window_.get_segment(window_.get_next_seq());
			if (!seg.sent) {
				transmit_segment(window_.get_next_seq());
				window_.advance_next_seq();
			} else {
				break;
			}
		}
	}

	/**
	 * 尝试发送FIN包结束连接
	 * 只有在所有数据段都被确认后才发送FIN
	 * 支持超时重传，最多MAX_FIN_RETRIES次
	 */
	void ReliableSender::try_send_fin() {
		uint64_t now = now_ms();
		if (fin_complete_) {
			return;
		}
		if (!fin_sent_) {
			if (!window_.all_acked()) {
				return;
			}
			PacketHeader fin{};
			fin.seq = isn_ + window_.total_segments() + 1;
			fin.ack = 0;
			fin.flags = FLAG_FIN;
			fin.wnd = window_size_;
			fin.len = 0;
			fin.sack_mask = 0;
			send_raw(fin, {});
			fin_sent_ = true;
			fin_last_send_ = now;
			fin_retry_count_ = 0;
			cout << "[DEBUG] Sent FIN\n";
			return;
		}
		if (now - fin_last_send_ > HANDSHAKE_TIMEOUT_MS &&
			fin_retry_count_ < MAX_FIN_RETRIES) {
			PacketHeader fin{};
			fin.seq = isn_ + window_.total_segments() + 1;
			fin.ack = 0;
			fin.flags = FLAG_FIN;
			fin.wnd = window_size_;
			fin.len = 0;
			fin.sack_mask = 0;
			send_raw(fin, {});
			fin_last_send_ = now;
			fin_retry_count_++;
			cout << "[DEBUG] Retrying FIN (attempt " << fin_retry_count_ << "/"
				 << MAX_FIN_RETRIES << ")\n";
		}
	}

	void ReliableSender::handle_fin_ack() {
		PacketHeader final_ack{};
		final_ack.seq = peer_isn_ + 1;
		final_ack.ack = isn_ + window_.total_segments() + 2;
		final_ack.flags = FLAG_ACK;
		final_ack.wnd = window_size_;
		final_ack.len = 0;
		final_ack.sack_mask = 0;
		send_raw(final_ack, {});
		fin_complete_ = true;
		cout << "[DEBUG] Received FIN+ACK, sent final ACK\n";
	}

	void ReliableSender::process_network() {
		Packet pkt{};
		sockaddr_in from{};
		if (wait_for_packet(pkt, from, 50)) {
			if (!same_endpoint(from, remote_)) {
				return;
			}
			if ((pkt.header.flags & FLAG_FIN) &&
				(pkt.header.flags & FLAG_ACK)) {
				handle_fin_ack();
				return;
			}
			if (pkt.header.flags & FLAG_ACK) {
				handle_ack(pkt);
			}
		}
	}

	int ReliableSender::run() {
		WSADATA wsa;  // WSA 数据结构
		// 2.2 版本
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			cerr << "WSAStartup failed\n";
			return -1;
		}

		sock_ = socket(AF_INET, SOCK_DGRAM, 0);
		if (!socket_valid(sock_)) {
			cerr << "Failed to create socket\n";
			return 1;
		}

		// 绑定本地端口（可选）
		if (local_port_ > 0) {
			sockaddr_in local_addr{};
			local_addr.sin_family = AF_INET;
			local_addr.sin_addr.s_addr = INADDR_ANY;
			local_addr.sin_port = htons(local_port_);
			if (bind(sock_, reinterpret_cast<const sockaddr*>(&local_addr),
					 sizeof(local_addr)) < 0) {
				cerr << "Failed to bind to local port " << local_port_ << "\n";
				return 1;
			}
			cout << "[DEBUG] Bound to local port " << local_port_ << "\n";
		} else {
			cout << "[DEBUG] Using system-assigned port\n";
		}

		// 使用 inet_addr 替代 inet_pton 以兼容 MinGW
		remote_.sin_addr.s_addr = inet_addr(dest_ip_.c_str());
		if (remote_.sin_addr.s_addr == INADDR_NONE) {
			cerr << "Invalid receiver address\n";
			return 1;
		}

		sockaddr_in local_addr{};
		int addr_len = sizeof(local_addr);
		if (getsockname(sock_, reinterpret_cast<sockaddr*>(&local_addr),
						&addr_len) != 0) {
			local_addr.sin_family = AF_INET;
			local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			local_addr.sin_port = 0;
		}
		isn_ = generate_isn(local_addr, remote_);

		if (!handshake()) {
			cerr << "Handshake failed\n";
			return 1;
		}

		ifstream in(file_path_, std::ios::binary);
		if (!in) {
			cerr << "Cannot open input file\n";
			return 1;
		}
		file_data_ = vector<uint8_t>((std::istreambuf_iterator<char>(in)),
									 std::istreambuf_iterator<char>());

		cout << "[DEBUG] File size: " << file_data_.size() << " bytes\n";
		window_.initialize(file_data_);
		cout << "[DEBUG] Total segments: " << window_.total_segments() << "\n";

		peer_wnd_ = peer_wnd_ ? peer_wnd_ : window_size_;
		congestion_ = CongestionControl(std::max<double>(2.0, peer_wnd_));

		cout << "[DEBUG] Starting transmission - Window: " << window_size_
			 << ", Initial cwnd: " << congestion_.get_cwnd()
			 << ", ssthresh: " << congestion_.get_ssthresh() << "\n";

		// 初始化全局超时检测
		last_ack_time_ = now_ms();

		while (!fin_complete_) {
			// 全局超时检测：长时间无ACK响应
			if (now_ms() - last_ack_time_ > GLOBAL_TIMEOUT_MS) {
				cerr << "[TIMEOUT] No ACK received for "
					 << GLOBAL_TIMEOUT_MS / 1000 << "s, connection lost\n";
				send_rst();
				return 1;
			}

			try_send_data();
			process_network();
			handle_timeouts();
			handle_window_probe();
			if (!data_timing_recorded_ && window_.all_acked()) {
				if (stats_.get_start_time() == 0) {
					stats_.set_start_time(now_ms());
				}
				stats_.set_end_time(now_ms());
				data_timing_recorded_ = true;
			}

			try_send_fin();

			if (fin_sent_ && !fin_complete_ &&
				fin_retry_count_ >= MAX_FIN_RETRIES) {
				cerr << "[WARN] FIN handshake failed after retries\n";
				break;
			}
		}

		if (!data_timing_recorded_) {
			if (stats_.get_start_time() == 0) {
				stats_.set_start_time(now_ms());
			}
			stats_.set_end_time(now_ms());
		}

		stats_.print_sender_stats(file_data_.size(), window_.total_segments(),
								  congestion_.get_cwnd(),
								  congestion_.get_ssthresh());

		if (!fin_complete_) {
			cerr << "[WARN] FIN handshake did not complete cleanly\n";
		}

		return fin_complete_ ? 0 : 1;
	}

}  // namespace rtp
