#include "congestion_control.h"

#include <iostream>

namespace rtp {
    using std::cout;
    using std::endl;
    CongestionControl::CongestionControl(double initial_ssthresh)
        : cwnd_(1.0),
          ssthresh_(initial_ssthresh),
          dup_ack_count_(0),
          in_fast_recovery_(false) {}

    void CongestionControl::on_new_ack() {
        // 收到新ACK，推进窗口
        dup_ack_count_ = 0;

        // 如果处于快速恢复，退出并设置cwnd为ssthresh
        if (in_fast_recovery_) {
            cwnd_ = ssthresh_;
            in_fast_recovery_ = false;
        }

        // 慢启动或拥塞避免
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
            cwnd_ += 1.0;
        }
    }

    bool CongestionControl::should_fast_retransmit() const {
        return dup_ack_count_ == 3 && !in_fast_recovery_;
    }

    void CongestionControl::on_fast_retransmit() {
        cout << "[LOSS] Detected 3 duplicate ACKs, triggering fast retransmit "
                "(cwnd: "
             << cwnd_ << " -> " << (cwnd_ / 2.0 + 3.0) << ")" << endl;

        ssthresh_ = std::max(2.0, cwnd_ / 2.0);
        cwnd_ = ssthresh_ + 3.0;
        in_fast_recovery_ = true;
    }

    void CongestionControl::on_timeout() {
        cout << "[TIMEOUT] Congestion control timeout (cwnd: " << cwnd_
             << " -> 1.0, ssthresh: " << ssthresh_ << " -> " << (cwnd_ / 2.0)
             << ")" << endl;

        ssthresh_ = std::max(2.0, cwnd_ / 2.0);
        cwnd_ = 1.0;
        dup_ack_count_ = 0;
        in_fast_recovery_ = false;
    }

    void CongestionControl::exit_fast_recovery() {
        cwnd_ = ssthresh_;
        in_fast_recovery_ = false;
    }

}  // namespace rtp
