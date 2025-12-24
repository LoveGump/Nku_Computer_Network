// send_window.h
// 发送窗口管理
#pragma once

#include <cstdint>
#include <vector>

#include "rtp.h"

namespace rtp {
	using std::size_t;
	using std::vector;

	/**
	 * 发送窗口管理
	 * 负责维护所有数据段的状态、滑动窗口的边界和发送逻辑
	 */
	class SendWindow {
	   public:
		// 数据段信息
		struct SegmentInfo {
			vector<uint8_t> data;		   // 段数据
			bool sent{false};			   // 是否已发送
			bool acked{false};			   // 是否已确认
			uint64_t last_send{0};		   // 最后发送时间（用于超时检测）
			uint64_t last_sack_retx{0};	   // 最后SACK重传时间（避免频繁重传）
			int retrans_count{0};		   // 重传次数（用于检测连接断开）
			uint64_t send_timestamp{0};	   // 首次发送时间戳
			bool is_retransmitted{false};  // 是否被重传过（Karn算法）
		};
		SendWindow();

		// 初始化分段，将文件数据按 MAX_PAYLOAD 大小分段
		void initialize(const vector<uint8_t>& file_data);

		// 标记段为已确认，seq: 段序号（从1开始）
		void mark_acked(uint32_t seq);

		// 获取段信息
		SegmentInfo& get_segment(uint32_t seq);

		// 检查所有段是否都已确认，左窗口边界推进到 total_segments_ + 1
		bool all_acked() const;

		// 获取已发送未确认段数量，发送数量 - 确认数量
		size_t inflight_count() const;

		// 获取总段数
		uint32_t total_segments() const { return total_segments_; }

		// 获取/设置基序号
		uint32_t get_base_seq() const { return base_seq_; }
		void set_base_seq(uint32_t seq) { base_seq_ = seq; }

		// 获取/设置下一个待发送序号
		uint32_t get_next_seq() const { return next_seq_; }
		void set_next_seq(uint32_t seq) { next_seq_ = seq; }
		// 推进下一个待发送序号
		void advance_next_seq() { next_seq_++; }

		// 推进base_seq到第一个未确认的段
		void advance_base_seq();

		// 计算实际窗口大小 取本地窗口、对端窗口、拥塞窗口、SACK位宽的最小值
		// 事实上，本地窗口和对端窗口均为 32 ，位宽也是 32
		size_t calculate_window_size(uint16_t local_window, uint16_t peer_window, double cwnd, size_t sack_bits) const;

	   private:
		vector<SegmentInfo> segments_;	// 所有数据段
		uint32_t total_segments_;		// 总段数
		uint32_t base_seq_;				// 窗口左边界（最小未确认序号）
		uint32_t next_seq_;				// 下一个待发送序号
	};

}  // namespace rtp
