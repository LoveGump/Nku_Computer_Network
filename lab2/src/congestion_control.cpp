#include "congestion_control.h"

#include <iostream>

namespace rtp {
	using std::cout;
	using std::endl;
	CongestionControl::CongestionControl(double initial_ssthresh)
		: cwnd_(1.0),
		  ssthresh_(initial_ssthresh),
		  dup_ack_count_(0),
		  in_fast_recovery_(false),
		  recover_seq_(0) {}

	bool CongestionControl::on_new_ack(uint32_t ack_seq, uint32_t next_seq) {
		// 收到新ACK，推进窗口
		dup_ack_count_ = 0;
		bool is_partial_ack = false;

		// NewReno: 如果处于快速恢复，检查是部分ACK还是完整ACK
		if (in_fast_recovery_) {
			if (ack_seq < recover_seq_) {
				// 部分ACK：只确认了部分数据，说明后面还有丢包
				// 收缩cwnd（抵消之前的膨胀），重传下一个段
				cwnd_ = std::max(cwnd_ - 1.0, ssthresh_);
				is_partial_ack = true;
				cout << "[NewReno] Partial ACK detected (ack=" << ack_seq
					 << ", recover=" << recover_seq_ << "), cwnd=" << cwnd_
					 << endl;
			} else {
				// 完整ACK：所有数据都确认了，退出快速恢复
				cwnd_ = ssthresh_;
				in_fast_recovery_ = false;
				cout << "[NewReno] Full ACK received, exiting fast recovery "
						"(cwnd="
					 << cwnd_ << ")" << endl;
			}
		}

		// 如果不在快速恢复（或刚退出），执行慢启动或拥塞避免
		if (!in_fast_recovery_) {
			if (cwnd_ < ssthresh_) {
				// 慢启动阶段：指数增长，每个ACK使cwnd加1
				cwnd_ += 1.0;
			} else {
				// 拥塞避免阶段：线性增长，每RTT增加1个MSS
				// 每个ACK增加 1/cwnd，cwnd个ACK后总共增加1
				cwnd_ += 1.0 / cwnd_;
			}
		}

		return is_partial_ack;
	}

	void CongestionControl::on_duplicate_ack() {
		dup_ack_count_++;

		// 如果已经在快速恢复中，继续膨胀窗口
		// 每个重复ACK代表一个离开网络的数据包，可以发送新包
		if (in_fast_recovery_) {
			cwnd_ += 1.0;
		}
	}

	bool CongestionControl::should_fast_retransmit() const {
		return dup_ack_count_ == 3 && !in_fast_recovery_;
	}

	void CongestionControl::on_fast_retransmit(uint32_t next_seq) {
		cout << "[LOSS] Detected 3 duplicate ACKs, triggering fast retransmit "
				"(cwnd: "
			 << cwnd_ << " -> " << (cwnd_ / 2.0 + 3.0) << ")" << endl;

		ssthresh_ = std::max(2.0, cwnd_ / 2.0);
		cwnd_ = ssthresh_ + 3.0;
		in_fast_recovery_ = true;
		recover_seq_ = next_seq;  // NewReno: 记录进入快速恢复时的最高序列号
	}

	void CongestionControl::on_timeout() {
		cout << "[TIMEOUT] Congestion control timeout (cwnd: " << cwnd_
			 << " -> 1.0, ssthresh: " << ssthresh_ << " -> " << (cwnd_ / 2.0)
			 << ")" << endl;

		ssthresh_ = std::max(2.0, cwnd_ / 2.0);
		cwnd_ = 1.0;
		dup_ack_count_ = 0;
		in_fast_recovery_ = false;
		recover_seq_ = 0;
	}

	void CongestionControl::exit_fast_recovery() {
		cwnd_ = ssthresh_;
		in_fast_recovery_ = false;
	}

}  // namespace rtp
