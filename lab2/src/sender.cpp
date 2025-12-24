#include "sender.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <cmath>

namespace rtp {
	using std::cerr;
	using std::cout;
	using std::endl;
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
	}  // namespace

	ReliableSender::ReliableSender(string dest_ip, uint16_t dest_port, string file_path, uint16_t window_size,
								   uint16_t local_port)
		: dest_ip_(std::move(dest_ip)),
		  dest_port_(dest_port),
		  local_port_(local_port),
		  file_path_(std::move(file_path)),
		  window_size_(std::min<uint16_t>(window_size, static_cast<uint16_t>(SACK_BITS))),
		  peer_wnd_(0),
		  isn_(0),
		  peer_isn_(0) {
		remote_.sin_family = AF_INET;		   // IPv4
		remote_.sin_port = htons(dest_port_);  // 目标端口
	}

	ReliableSender::~ReliableSender() {
		if (socket_valid(sock_)) {
			closesocket(sock_);
		}
	}

	bool ReliableSender::wait_for_packet(Packet& pkt, sockaddr_in& from, int timeout_ms) {
		fd_set rfds;  // 声明读文件描述符集合
		FD_ZERO(&rfds);
		FD_SET(sock_, &rfds);
		timeval tv{};  // 超时结构体
		if (timeout_ms >= 0) {
			tv.tv_sec = timeout_ms / 1000;
			tv.tv_usec = (timeout_ms % 1000) * 1000;
		}
		// 等待数据可读或超时
		int rv = select(0, &rfds, nullptr, nullptr, timeout_ms >= 0 ? &tv : nullptr);
		if (rv <= 0) {
			// 超时或出错
			return false;
		}
		sockaddr_in peer{};	 // 发送方地址
		socklen_t len = sizeof(peer);
		vector<uint8_t> buf(2048);
		int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
						 reinterpret_cast<sockaddr*>(&peer), &len);
		if (n <= 0) {
			// 接收失败
			return false;
		}
		// 检验校验和并	将数据包解析到 pkt
		if (!parse_packet(buf.data(), static_cast<size_t>(n), pkt)) {
			return false;
		}
		// 返回发送方地址
		from = peer;
		return true;
	}

	int ReliableSender::send_raw(const PacketHeader& hdr, const vector<uint8_t>& payload) {
		// 序列化并发送数据包
		auto buffer = serialize_packet(hdr, payload);
		return sendto(sock_, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
					  reinterpret_cast<const sockaddr*>(&remote_), sizeof(remote_));
	}

	/**
	 * 发送RST段，强制终止连接
	 * 用于握手失败、校验错误、状态异常等场景
	 */
	void ReliableSender::send_rst() {
		// 构造RST包
		PacketHeader rst{};
		rst.seq = isn_ + 1;
		rst.ack = peer_isn_ + 1;
		rst.flags = FLAG_RST;
		rst.wnd = 0;
		rst.len = 0;
		rst.sack_mask = 0;
		// 发送RST包
		send_raw(rst, {});
		cout << "[RST] Sent RST segment to reset connection" << endl;
	}

	/**
	 * 执行三次握手建立连接（主动方）
	 * 流程：SYN -> SYN+ACK -> ACK
	 * 返回true表示握手成功
	 */
	bool ReliableSender::handshake() {
		// 构建syn包
		PacketHeader syn{};
		syn.seq = isn_;
		syn.ack = 0;
		syn.wnd = window_size_;
		syn.len = 0;
		syn.flags = FLAG_SYN;
		syn.sack_mask = 0;

		// 主动发送SYN包，等待SYN+ACK响应，最多5次重试
		for (int attempt = 0; attempt < MAX_HANDSHAKE_RETRIES; ++attempt) {
			cout << "[DEBUG] Sending SYN (attempt " << (attempt + 1) << "/" << MAX_HANDSHAKE_RETRIES << ")" << endl;
			send_raw(syn, {});	// 发送SYN包
			Packet pkt{};
			sockaddr_in from{};

			// 等待SYN+ACK响应，超时重试
			if (!wait_for_packet(pkt, from, HANDSHAKE_TIMEOUT_MS)) {
				// 超时重试
				continue;
			}
			if (!same_endpoint(from, remote_)) {
				// 非预期地址，忽略
				cout << "[DEBUG] Ignoring handshake response from unexpected peer" << endl;
				continue;
			}

			// 收到RST段，连接被对方重置
			if (pkt.header.flags & FLAG_RST) {
				cerr << "[RST] Received RST during handshake, connection reset by peer" << endl;
				return false;
			}

			// 收到SYN+ACK段，发送ACK完成握手
			if ((pkt.header.flags & FLAG_SYN) && (pkt.header.flags & FLAG_ACK) && pkt.header.ack == syn.seq + 1) {
				// 记录对端ISN和窗口大小
				peer_isn_ = pkt.header.seq;
				peer_wnd_ = pkt.header.wnd;
				cout << "[DEBUG] Received SYN+ACK, peer window size: " << peer_wnd_ << endl;
				// 发送ACK包完成握手
				PacketHeader ack{};
				ack.seq = isn_ + 1;
				ack.ack = peer_isn_ + 1;
				ack.flags = FLAG_ACK;
				ack.wnd = window_size_;
				ack.len = 0;
				ack.sack_mask = 0;
				send_raw(ack, {});
				cout << "[DEBUG] Handshake completed successfully" << endl;
				return true;
			}
		}
		// 握手失败
		cerr << "[WARN] Handshake failed after retries" << endl;
		send_rst();	 // 发送RST终止连接
		return false;
	}

	/**
	 * 发送或重传指定序号的数据段
	 * @param seq 段序号（相对序号，从1开始）
	 * 如果是重传，会记录统计信息
	 */
	void ReliableSender::transmit_segment(uint32_t seq) {

		// 获取段信息
		auto& seg = window_.get_segment(seq);
		bool is_retransmit = seg.sent;	// 已发送过则为重传

		// 检查重传次数，超过限制认为连接失败
		if (is_retransmit) {
			seg.retrans_count++; 			// 增加重传计数
			seg.is_retransmitted = true;  	// 标记为重传（Karn算法计算时间需要用到）
			if (seg.retrans_count > MAX_RETRANSMITS) {
				// 超过最大重传次数15，连接断开
				cerr << "[ERROR] Segment " << seq << " exceeded max retransmits (" << MAX_RETRANSMITS
					 << "), connection lost" << endl;
				send_rst();	 // 发送RST终止连接
			}
		} else {
			// 首次发送，记录时间戳用于RTT测量（Karn算法更新时间）
			seg.send_timestamp = now_ms();
			seg.is_retransmitted = false;
		}

		// 构造数据包头
		PacketHeader hdr{};
		hdr.seq = isn_ + seq;
		hdr.ack = 0;			// 单向发送，无需ACK
		hdr.flags = FLAG_DATA;	// 数据段
		hdr.wnd = window_size_;
		hdr.len = static_cast<uint16_t>(seg.data.size());
		hdr.sack_mask = 0;

		// 记录第一个数据包发送时间，作为全局的传输开始时间
		if (stats_.get_start_time() == 0) {
			stats_.set_start_time(now_ms());
		}

		// 发送数据包
		send_raw(hdr, seg.data);
		seg.sent = true;			// 标记为已发送
		seg.last_send = now_ms();

		// 记录重传统计
		if (is_retransmit) {
			// 如果是重传，记录统计信息
			stats_.record_retransmit();
		}
	}

	void ReliableSender::add_acked_bytes(uint32_t seq) {
		if (seq == 0 || seq > window_.total_segments()) {
			return;
		}
		auto& seg = window_.get_segment(seq);
		if (!seg.acked) {
			bytes_acked_ += seg.data.size();
		}
	}

	void ReliableSender::report_progress(bool force) {
		if (file_data_.empty()) {
			return;	 // 没有数据无需显示进度
		}

		uint64_t now = now_ms();
		int percent = static_cast<int>((bytes_acked_ * 100) / std::max<size_t>(1, file_data_.size()));
		percent = std::min(100, std::max(0, percent));

		if (!force) {
			if (percent == last_progress_percent_ && now - last_progress_print_ < 500) {
				return;	 // 进度未变化或打印过于频繁
			}
			if (now - last_progress_print_ < 500) {
				return;
			}
		}

		last_progress_print_ = now;
		last_progress_percent_ = percent;
		std::printf("\rProgress: %3d%% (%zu/%zu bytes)", percent, static_cast<size_t>(bytes_acked_),
					static_cast<size_t>(file_data_.size()));
		std::fflush(stdout);
		if (force && percent >= 100) {
			std::printf("\n");
		}
	}

	/**
	 * 处理新ACK（推进窗口）
	 * ack: 新的累积确认序号
	 * 标记所有 < ack 的段为已确认，并调用拥塞控制
	 */
	void ReliableSender::handle_new_ack(uint32_t ack) {

		// 测量RTT（Karn算法：只测量未重传的段来更新RTT）
		for (uint32_t i = window_.get_base_seq(); i < ack && i <= window_.total_segments(); ++i) {
			// 找到第一个未重传且已发送的段
			auto& seg = window_.get_segment(i);
			if (!seg.acked && seg.sent && !seg.is_retransmitted && seg.send_timestamp > 0) {
				// 计算RTT样本
				uint64_t rtt_sample = now_ms() - seg.send_timestamp;
				update_rto(rtt_sample);	 // 更新RTO
				// 只需要使用第一个符合条件的段进行RTT测量即可
				break;	
			}
		}

		// 标记所有被确认的段
		for (uint32_t s = window_.get_base_seq(); s < ack && s <= window_.total_segments(); ++s) {
			add_acked_bytes(s);	// 用于显示
			window_.mark_acked(s);	// 更新窗口标记
		}

		// 发送方滑动窗口：推进窗口左边界
		window_.set_base_seq(ack);

		//  检测部分ACK并处理
		// on_new_ack来更新拥塞控制窗口和拥塞控制状态
		bool is_partial_ack = congestion_.on_new_ack(ack, window_.get_next_seq()); 

		if (is_partial_ack) {
			// 部分ACK：重传下一个未确认的段，不进入快速恢复
			uint32_t next_unacked = ack;
			if (next_unacked <= window_.total_segments()) {
				auto& seg = window_.get_segment(next_unacked);
				if (!seg.acked) {
					cout << "[NewReno] Retransmitting next unacked segment: " << next_unacked << endl;
					transmit_segment(next_unacked);
				}
			}
		}
	}


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
		cout << "[探测] 发送窗口探测包 seq=" << hdr.seq << " backoff=" << persist_backoff_ << endl;
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



	/**
	 * 更新RTO（Jacobson/Karels算法）
	 * rtt_sample: RTT样本（毫秒）
	 * 公式：
	 *   SRTT = (1-α) × SRTT + α × RTT  (α = 1/8)
	 *   RTTVAR = (1-β) × RTTVAR + β × |SRTT - RTT|  (β = 1/4)
	 *   RTO = SRTT + 4 × RTTVAR
	 */
	void ReliableSender::update_rto(uint64_t rtt_sample) {
		constexpr double ALPHA = 0.125;	 // 1/8
		constexpr double BETA = 0.25;	 // 1/4
		constexpr int MIN_RTO = 20;	 // 最小RTO 20ms
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

		// // 只在RTO值改变时打印日志
		// if (rto_ != old_rto) {
		// 	cout << "[RTO] RTT=" << rtt_sample << "ms, SRTT=" << static_cast<int>(srtt_)
		// 		 << "ms, RTTVAR=" << static_cast<int>(rttvar_) << "ms, RTO=" << rto_ << "ms (changed from " << old_rto
		// 		 << "ms)" << endl;
		// }
	}

	/**
	 * 处理重复ACK
	 * 累积3个重复ACK后触发快速重传
	 */
	void ReliableSender::handle_duplicate_ack(uint32_t ack) {
		congestion_.on_duplicate_ack();
		// 检测是否触发快速重传
		if (congestion_.should_fast_retransmit()) {
			// 触发快速重传
			congestion_.on_fast_retransmit(window_.get_next_seq());
			fast_retransmit();
		}
	}

	/**
	 * 处理选择性确认（SACK）
	 * @param ack 累积确认序号
	 * @param mask SACK掩码，标记ack之后32个段的到达情况
	 * 根据掩码标记已到达的段，并重传缺口段
	 */
	void ReliableSender::handle_sack(uint32_t ack, uint32_t mask) {
		// 标记SACK确认的段
		for (size_t i = 0; i < SACK_BITS; ++i) {
			if (mask & (1u << i)) {
				uint32_t seq = ack + 1 + static_cast<uint32_t>(i);
				add_acked_bytes(seq);
				window_.mark_acked(seq);
			}
		}

		// 处理SACK缺口重传（限速：单次ACK最多重传4个）
		int gap_retx_count = 0;
		uint64_t now = now_ms();
		for (size_t i = 0; i < SACK_BITS; ++i) {
			// 检测缺口段
			uint32_t seq = ack + 1 + static_cast<uint32_t>(i);
			if (seq > window_.total_segments()) {
				// 超出总段数，停止检查
				break;
			}
			// 获取段信息
			auto& seg = window_.get_segment(seq);
			if (seg.sent && !seg.acked && !(mask & (1u << i))) {
				// 该段已发送但未确认，且不在SACK中 -> 缺口
				uint64_t last_gap = seg.last_sack_retx ? seg.last_sack_retx : seg.last_send;  // 上次发送时间
				if (gap_retx_count < MAX_SACK_RETX_PER_ACK && now >= last_gap + MIN_GAP_RETX_INTERVAL_MS) {
					// 限速重传：避免频繁重传同一个段
					seg.last_sack_retx = now;
					++gap_retx_count;
					cout << "[RETRANSMIT] SACK gap seq=" << seq << endl;
					// 重传缺口段
					// 2
					transmit_segment(seq);
				}
			}
		}
	}

	void ReliableSender::handle_ack(const Packet& pkt) {

		// 更新最后收到ACK的时间（用于全局超时检测）
		last_ack_time_ = now_ms();
		// 获取接收方窗口大小，拥塞控制时会更新min(对端窗口, 位宽)
		uint16_t new_peer_wnd = std::min<uint16_t>(pkt.header.wnd, static_cast<uint16_t>(SACK_BITS));

		// 检测零窗口状态，启动/停止持续计时器
		if (new_peer_wnd == 0 && !zero_window_) {
			// 进入零窗口状态
			zero_window_ = true;
			persist_backoff_ = 0;	// 重置退避计数
			persist_timer_ = now_ms() + 5000;  // 首次探测：5秒
			cout << "[windows]: zero window, start persist timer" << endl;
		} else if (new_peer_wnd > 0 && zero_window_) {
			// 离开零窗口状态
			zero_window_ = false;
			persist_backoff_ = 0;
			cout << "[windows]: reopen " << new_peer_wnd << endl;
		}

		// 更新对端窗口大小
		peer_wnd_ = new_peer_wnd;

		// 处理ACK
		uint32_t ack_abs = pkt.header.ack;
		if (ack_abs <= isn_) {
			// 小于等于ISN，忽略无效ACK
			return;
		}
		// 转换为相对序号
		uint32_t ack = ack_abs - isn_;
		if (ack > window_.get_base_seq()) {
			// 新ACK，推进窗口
			handle_new_ack(ack);
		} else if (ack == window_.get_base_seq() && window_.get_base_seq() <= window_.total_segments()) {
			// 重复ACK，处理快速重传
			handle_duplicate_ack(ack);
		}

		// 处理SACK掩码
		handle_sack(ack, pkt.header.sack_mask);
		// 现在窗口的位置应该是当前ack序号
		// 滑动窗口：将窗口左边界推进到第一个未确认段
		window_.advance_base_seq();
		report_progress();
	}

	/**
	 * 检查并处理超时的数据段
	 * 遍历窗口内所有已发送但未确认的段
	 * 如果超过DATA_TIMEOUT_MS，则触发超时重传和拥塞控制
	 */
	void ReliableSender::handle_timeouts() {
		uint64_t now = now_ms();  // 获取当前时间
		// 遍历窗口内所有已发送但未确认的段
		for (uint32_t i = window_.get_base_seq(); i <= window_.total_segments(); ++i) {
			auto& seg = window_.get_segment(i);
			// 计时器：动态RTO，检测ack是否已经超时
			if (!seg.acked && seg.sent && now - seg.last_send > static_cast<uint64_t>(rto_)) {
				// 段超时，触发重传
				stats_.record_timeout();
				cout << "[TIMEOUT] Packet seq=" << i << " timed out after " << (now - seg.last_send)
					 << "ms (RTO=" << rto_ << "ms), retransmitting" << endl;
				// 拥塞控制：超时进入慢启动
				congestion_.on_timeout();
				// 超时后倍增RTO（Karn算法）
				rto_ = std::min(rto_ * 2, 60000);  // 最大RTO 60s
				transmit_segment(i);
			}
		}
	}

	// 快速重传窗口基序号的数据段
	void ReliableSender::fast_retransmit() {
		if (window_.get_base_seq() <= window_.total_segments()) {
			// 记录快速重传统计
			stats_.record_fast_retransmit();
			cout << "[RETRANSMIT] Fast retransmit seq=" << window_.get_base_seq() << endl;
			// 重传基序号段
			transmit_segment(window_.get_base_seq());
		}
	}

	/**
	 * 尝试发送新数据
	 * 根据窗口大小（本地、对端、拥塞窗口的最小值）发送数据
	 * 只发送未发送过的段，不处理重传
	 */
	void ReliableSender::try_send_data() {
		// 零窗口时由窗口探测机制处理，这里直接退出
		if (peer_wnd_ == 0) {
			return;	
		}

		// 计算实际窗口大小 = min(本地窗口, 对端窗口, floor(cwnd), SACK位宽)
		size_t window_cap = window_.calculate_window_size(window_size_, peer_wnd_, congestion_.get_cwnd(), SACK_BITS);

		// 连续发送新数据段，直到窗口满或无新数据
		while (window_.get_next_seq() <= window_.total_segments() &&
			   window_.get_next_seq() < window_.get_base_seq() + window_cap) {
			// 获取下一个数据段并发送
			auto& seg = window_.get_segment(window_.get_next_seq()); // 获得数据段信息
			if (!seg.sent) {
				// 发送数据段，发送窗口更新下一个序号
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
		uint64_t now = now_ms();  // 获取当前时间
		if (fin_complete_) {
			// 连接已完成，无需发送FIN
			return;
		}
		if (!fin_sent_) {
			if (!window_.all_acked()) {
				// 还有未确认的数据段，不能发送FIN
				return;
			}
			// 发送FIN包
			PacketHeader fin{};
			fin.seq = isn_ + window_.total_segments() + 1;
			fin.ack = 0;
			fin.flags = FLAG_FIN;
			fin.wnd = window_size_;
			fin.len = 0;
			fin.sack_mask = 0;
			send_raw(fin, {});
			// 记录FIN发送状态
			fin_sent_ = true;
			fin_last_send_ = now;
			fin_retry_count_ = 0;
			cout << "[DEBUG] Sent FIN" << endl;
			return;
		}
		// 处理FIN重传
		if (now - fin_last_send_ > HANDSHAKE_TIMEOUT_MS && fin_retry_count_ < MAX_FIN_RETRIES) {
			// 重传FIN包
			PacketHeader fin{};
			fin.seq = isn_ + window_.total_segments() + 1;
			fin.ack = 0;
			fin.flags = FLAG_FIN;
			fin.wnd = window_size_;
			fin.len = 0;
			fin.sack_mask = 0;
			send_raw(fin, {});
			// 更新重传状态
			fin_last_send_ = now;
			fin_retry_count_++;
			cout << "[DEBUG] Retrying FIN (attempt " << fin_retry_count_ << "/" << MAX_FIN_RETRIES << ")" << endl;
		}
	}

	void ReliableSender::handle_fin_ack() {
		// 发送最终ACK包
		PacketHeader final_ack{};
		final_ack.seq = peer_isn_ + 1;
		final_ack.ack = isn_ + window_.total_segments() + 2;
		final_ack.flags = FLAG_ACK;
		final_ack.wnd = window_size_;
		final_ack.len = 0;
		final_ack.sack_mask = 0;
		send_raw(final_ack, {});
		// 标记断开连接完成
		fin_complete_ = true;
		cout << "[DEBUG] Received FIN+ACK, sent final ACK, connection closed" << endl;
	}

	// 处理网络事件（接收ACK等）50ms超时
	void ReliableSender::process_network() {
		Packet pkt{};
		sockaddr_in from{};
		// 非阻塞接收数据包，等待50ms
		if (wait_for_packet(pkt, from, 50)) {
			// 一旦接收到数据，处理数据包
			if (!same_endpoint(from, remote_)) {
				// 非预期地址，忽略
				return;
			}
			if ((pkt.header.flags & FLAG_FIN) && (pkt.header.flags & FLAG_ACK)) {
				// 处理FIN+ACK包，完成连接关闭
				handle_fin_ack();
				return;
			}
			if (pkt.header.flags & FLAG_ACK) {
				// 如果收到ACK包，处理ACK
				handle_ack(pkt);
			}
		}
	}

	int ReliableSender::run() {
		WSADATA wsa;  // WSA 
		// 2.2 版本
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			cerr << "WSAStartup failed" << endl;
			return -1;
		}

		// 创建UDP套接字
		sock_ = socket(AF_INET, SOCK_DGRAM, 0);
		if (!socket_valid(sock_)) {
			cerr << "Failed to create socket" << endl;
			return 1;
		}

		// 绑定本地端口
		sockaddr_in local_addr{};
		local_addr.sin_family = AF_INET;
		local_addr.sin_addr.s_addr = INADDR_ANY;
		local_addr.sin_port = htons(local_port_);
		if (bind(sock_, reinterpret_cast<const sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
			cerr << "Failed to bind to local port " << local_port_ << endl;
			return 1;
		}
		cout << "[DEBUG] Bound to local port " << local_port_ << endl;

		//	 设置要发送的目标地址
		remote_.sin_addr.s_addr = inet_addr(dest_ip_.c_str());
		if (remote_.sin_addr.s_addr == INADDR_NONE) {
			cerr << "Invalid receiver address" << endl;
			return 1;
		}

		// 生成初始序列号
		isn_ = generate_isn(local_addr, remote_);

		// 执行三次握手
		if (!handshake()) {
			// 握手失败，报错退出
			cerr << "Handshake failed" << endl;
			return 1;
		}

		// 读取输入文件
		ifstream in(file_path_, std::ios::binary);
		if (!in) {
			cerr << "Cannot open input file" << endl;
			return 1;
		}
		// 按字节读取整个文件到内存
		file_data_ = vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		bytes_acked_ = 0;				  // 已确认字节数清零
		last_progress_percent_ = -1;	  // 进度百分比初始化
		last_progress_print_ = now_ms();  // 进度打印时间初始化

		// 初始化发送窗口，将文件数据分段
		cout << "[DEBUG] File size: " << file_data_.size() << " bytes" << endl;
		window_.initialize(file_data_);
		cout << "[DEBUG] Total segments: " << window_.total_segments() << endl;
		// 确保初始的时候对端窗口非零
		peer_wnd_ = peer_wnd_ ? peer_wnd_ : window_size_;  
		// 初始化拥塞控制
		congestion_ = CongestionControl(std::max<double>(2.0, peer_wnd_));// 初始化拥塞窗口为对端窗口和2的最大值

		cout << "[DEBUG] Starting transmission - Window: " << window_size_
			 << ", Initial cwnd: " << congestion_.get_cwnd() << ", ssthresh: " << congestion_.get_ssthresh() << endl;

		// 初始化全局超时检测
		last_ack_time_ = now_ms();

		while (!fin_complete_) {
			// 没有完成连接关闭，持续运行
			// 开始正式循环发送数据包

			// 全局超时检测：长时间没有接ACK响应
			if (now_ms() - last_ack_time_ > GLOBAL_TIMEOUT_MS) {
				// 30s无响应，认为连接断开,发送RST
				cerr << "[TIMEOUT] No ACK received for " << GLOBAL_TIMEOUT_MS / 1000 << "s, connection lost" << endl;
				send_rst();
				return 1;
			}

			// 发送数据、处理网络事件、处理超时和窗口探测
			try_send_data();
			// 每次循环处理网络事件，这里不会丢数据包，数据包到达的时候，内核会缓存
			// 只要循环足够快，处理网络事件就能及时响应
			process_network();
			// 处理数据段超时重传
			handle_timeouts();
			// 处理窗口探测
			handle_window_probe();

			// 记录数据传输时间点
			if (!data_timing_recorded_ && window_.all_acked()) {
				// 如果全部都确认，记录结束时间
				stats_.set_end_time(now_ms());
				data_timing_recorded_ = true;
			}

			// 尝试发送FIN包
			try_send_fin();

			// 检查FIN重传次数，超过限制则放弃
			if (fin_sent_ && !fin_complete_ && fin_retry_count_ >= MAX_FIN_RETRIES) {
				cerr << "[WARN] FIN handshake failed after retries" << endl;
				break;
			}
		}

		// 记录结束时间
		if (!data_timing_recorded_) {
			if (stats_.get_start_time() == 0) {
				stats_.set_start_time(now_ms());
			}
			stats_.set_end_time(now_ms());
		}

		report_progress(true);

		// 打印传输统计信息
		stats_.print_sender_stats(file_data_.size(), window_.total_segments(), congestion_.get_cwnd(),
								  congestion_.get_ssthresh());

		if (!fin_complete_) {
			cerr << "[WARN] FIN handshake did not complete cleanly" << endl;
		}

		return !fin_complete_;
	}
}  // namespace rtp
