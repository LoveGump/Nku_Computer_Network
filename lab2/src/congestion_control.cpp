// congestion_control.cpp
// 拥塞控制算法实现（TCP Reno）
#include "congestion_control.h"

#include <iostream>

namespace rtp {
	using std::cout;
	using std::endl;
	CongestionControl::CongestionControl(double initial_ssthresh)
		: cwnd_(1.0), ssthresh_(initial_ssthresh), dup_ack_count_(0), in_fast_recovery_(false) {}

	void CongestionControl::on_new_ack() {
		dup_ack_count_ = 0;	 // 重置重复ACK计数

		// Reno: 在快速恢复中收到“新ACK”表示丢失段已被修复，退出快速恢复
		// 退出时将 cwnd 设为 ssthresh，等待后续 ACK 再进入拥塞避免的线性增长
		if (in_fast_recovery_) {
			cwnd_ = ssthresh_;
			in_fast_recovery_ = false;
			cout << "[Reno] New ACK received, exiting fast recovery (cwnd=" << cwnd_ << ")" << endl;
			return;
		}

		// 非快速恢复：慢启动或拥塞避免
		if (cwnd_ < ssthresh_) {
			// 慢启动阶段：指数增长，每个ACK使cwnd加1
			cwnd_ += 1.0;
		} else {
			// 拥塞避免阶段：线性增长，每RTT增加1个MSS
			// 每个ACK增加 1/cwnd，cwnd个ACK后总共增加1
			cwnd_ += 1.0 / cwnd_;
		}
	}

	void CongestionControl::on_duplicate_ack() {
		dup_ack_count_++;

		// 如果已经在快速恢复中，继续膨胀窗口
		// 每个重复ACK代表一个离开网络的数据包，可以发送新包
		if (in_fast_recovery_) {
			cwnd_ += 1.0;  // 增加cwnd以允许发送新数据
		}
	}

	bool CongestionControl::should_fast_retransmit() const { return dup_ack_count_ == 3 && !in_fast_recovery_; }

	void CongestionControl::on_fast_retransmit() {
		// 快重传
		cout << "[LOSS] Detected 3 duplicate ACKs, triggering fast retransmit (cwnd: "
			 << cwnd_ << " -> " << (cwnd_ / 2.0 + 3.0) << ")" << endl;

		ssthresh_ = std::max(4.0, cwnd_ / 2.0);	 // 最小为4
		cwnd_ = ssthresh_ + 3.0;					// 增加3个MSS以应对网络中离开的包
		in_fast_recovery_ = true;
	}

	
	void CongestionControl::on_timeout() {
		cout << "[TIMEOUT] Congestion control timeout (cwnd: " << cwnd_ << " -> 1.0, ssthresh: " << ssthresh_ << " -> "
			 << (cwnd_ / 2.0) << ")" << endl;
		// 慢启动
		ssthresh_ = std::max(4.0, cwnd_ / 2.0); // 最小为4
		cwnd_ = 1.0;
		// 重置拥塞控制状态  		
		dup_ack_count_ = 0;	
		in_fast_recovery_ = false;
	}
}  // namespace rtp
