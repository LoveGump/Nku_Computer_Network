#pragma once

#include <algorithm>
#include <cstdint>

namespace rtp {
	/**
	 * TCP NewReno 拥塞控制算法
	 * 实现慢启动、拥塞避免、快速重传和快速恢复
	 * NewReno改进：在快速恢复期间检测部分ACK，持续重传丢失段
	 */
	class CongestionControl {
	   public:
		explicit CongestionControl(double initial_ssthresh = 64.0);

		// 收到新ACK时调用
		// 根据当前状态执行慢启动或拥塞避免算法
		// 返回true表示检测到部分ACK，需要重传下一个段
		// 参数: ack_seq = 收到的ACK序列号, next_seq = 最高待发送序号
		bool on_new_ack(uint32_t ack_seq, uint32_t next_seq);

		// 收到dupACK时调用
		// 如果在快速恢复中，则线性增加cwnd，发送新的数据
		void on_duplicate_ack();

		// 检测到3个重复ACK，触发快速重传
		// 返回true表示应该执行快速重传
		bool should_fast_retransmit() const;

		// 执行快速重传后拥塞控制部分的处理
		// ssthresh = cwnd/2, cwnd = ssthresh + 3, 进入快速恢复
		// 记录recover_seq用于部分ACK检测
		void on_fast_retransmit(uint32_t next_seq);

		// 超时事件处理
		// ssthresh = cwnd/2, cwnd = 1, 退出快速恢复
		void on_timeout();

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
		double cwnd_;			  // 拥塞窗口
		double ssthresh_;		  // 慢启动阈值
		uint32_t dup_ack_count_;  // 重复ACK计数
		bool in_fast_recovery_;	  // 是否处于快速恢复状态
		uint32_t recover_seq_;	  // NewReno: 进入快速恢复时的最高序列号
	};

}  // namespace rtp
