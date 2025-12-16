#pragma once

#include <cstdint>
#include <iostream>

namespace rtp {
    using std::size_t;
    /**
     * 传输统计信息收集器
     * 负责记录和计算传输过程中的各种性能指标
     */
    class TransferStats {
       public:
        TransferStats();

        // 记录重传
        void record_retransmit() { retransmit_count_++; }

        // 记录超时
        void record_timeout() { timeout_count_++; }

        // 记录快速重传
        void record_fast_retransmit() { fast_retransmit_count_++; }

        // 记录时间（开始/结束）
        void set_start_time(uint64_t time) { start_time_ = time; }
        void set_end_time(uint64_t time) { end_time_ = time; }

        // 获取统计数据
        uint32_t get_retransmit_count() const { return retransmit_count_; }
        uint32_t get_timeout_count() const { return timeout_count_; }
        uint32_t get_fast_retransmit_count() const {
            return fast_retransmit_count_;
        }
        uint64_t get_start_time() const { return start_time_; }
        uint64_t get_end_time() const { return end_time_; }

        // 计算传输耗时（秒）
        // 从start_time到end_time的时间差
        double get_elapsed_seconds() const;

        // 计算吞吐率（MiB/s）
        // 吞吐率 = 字节数 / 耗时 / 1024 / 1024
        double get_throughput(size_t bytes) const;

        // 计算丢包率（百分比）
        // 丢包率 = 重传次数 / 总段数 * 100%
        double get_loss_rate(uint32_t total_segments) const;

        // 打印发送端统计信息
        void print_sender_stats(size_t file_size, uint32_t total_segments,
                                double cwnd, double ssthresh) const;

        // 打印接收端统计信息
        void print_receiver_stats(size_t bytes_received, uint32_t total_packets,
                                  uint32_t out_of_order,
                                  uint32_t duplicates) const;

       private:
        uint32_t retransmit_count_;       // 重传次数
        uint32_t timeout_count_;          // 超时次数
        uint32_t fast_retransmit_count_;  // 快速重传次数
        uint64_t start_time_;             // 开始时间（毫秒）
        uint64_t end_time_;               // 结束时间（毫秒）
    };

}  // namespace rtp
