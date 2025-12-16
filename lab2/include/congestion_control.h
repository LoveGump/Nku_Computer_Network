#pragma once

#include <algorithm>
#include <cstdint>

namespace rtp {
    /**
     * TCP Reno 拥塞控制算法
     * 实现慢启动、拥塞避免、快速重传和快速恢复
     */
    class CongestionControl {
       public:
        explicit CongestionControl(double initial_ssthresh = 16.0);

        // 收到新ACK时调用（推进窗口）
        // 根据当前状态执行慢启动或拥塞避免算法
        void on_new_ack();

        // 收到重复ACK时调用
        // 如果在快速恢复中，则线性增加cwnd
        void on_duplicate_ack();

        // 检测到3个重复ACK，触发快速重传
        // 返回true表示应该执行快速重传
        bool should_fast_retransmit() const;

        // 执行快速重传后的处理
        // ssthresh = cwnd/2, cwnd = ssthresh + 3, 进入快速恢复
        void on_fast_retransmit();

        // 超时事件处理
        // ssthresh = cwnd/2, cwnd = 1, 退出快速恢复
        void on_timeout();

        // 退出快速恢复
        // 设置cwnd = ssthresh
        void exit_fast_recovery();

        // 获取当前拥塞窗口大小
        double get_cwnd() const { return cwnd_; }

        // 获取慢启动阈值
        double get_ssthresh() const { return ssthresh_; }

        // 是否处于快速恢复状态
        bool in_fast_recovery() const { return in_fast_recovery_; }

        // 获取重复ACK计数
        uint32_t get_dup_ack_count() const { return dup_ack_count_; }

        // 重置重复ACK计数
        void reset_dup_ack_count() { dup_ack_count_ = 0; }

       private:
        double cwnd_;             // 拥塞窗口（段数）
        double ssthresh_;         // 慢启动阈值
        uint32_t dup_ack_count_;  // 重复ACK计数
        bool in_fast_recovery_;   // 是否处于快速恢复状态
    };

}  // namespace rtp
