#include "send_window.h"

#include <algorithm>
#include <cmath>

namespace rtp {
	using std::size_t;
	using std::vector;

	SendWindow::SendWindow() : total_segments_(0), base_seq_(1), next_seq_(1) {}

	void SendWindow::initialize(const vector<uint8_t>& file_data) {
		// 计算总段数，为每段分配空间
		total_segments_ = static_cast<uint32_t>((file_data.size() + MAX_PAYLOAD - 1) / MAX_PAYLOAD);
		segments_.assign(total_segments_, {});

		// 将文件数据分段存储
		for (uint32_t i = 0; i < total_segments_; ++i) {
			size_t start = i * MAX_PAYLOAD;
			size_t len = std::min<size_t>(MAX_PAYLOAD, file_data.size() - start);
			segments_[i].data.insert(segments_[i].data.end(), file_data.begin() + start,
									 file_data.begin() + start + len);
		}

		// 初始化基序号和下一个待发送序号
		base_seq_ = 1;
		next_seq_ = 1;
	}

	void SendWindow::mark_acked(uint32_t seq) {
		if (seq == 0 || seq > total_segments_) {
			// 序号无效，忽略
			return;
		}
		auto& seg = segments_[seq - 1];	 // 序号从1开始
		if (!seg.acked) {
			// 标记为已确认
			seg.acked = true;
			seg.last_sack_retx = 0;
		}
	}

	SendWindow::SegmentInfo& SendWindow::get_segment(uint32_t seq) { return segments_[seq - 1]; }

	bool SendWindow::all_acked() const {
		// 当所有段都确认后，base_seq_ 会被 advance_base_seq() 推进到 total_segments_ + 1
		return base_seq_ > total_segments_;
	}

	size_t SendWindow::inflight_count() const {
		// 已发送但未确认的段数 = next_seq_ - base_seq_
		if (next_seq_ >= base_seq_) {
			return next_seq_ - base_seq_;
		}
		return 0;
	}

	void SendWindow::advance_base_seq() {
		// 如果当前 base_seq_ 段已确认，左窗口推进到下一个未确认段
		while (base_seq_ <= total_segments_ && segments_[base_seq_ - 1].acked) {
			base_seq_++;
		}
	}

	size_t SendWindow::calculate_window_size(uint16_t local_window, uint16_t peer_window, double cwnd,
											 size_t sack_bits) const {
		size_t window_cap = std::min<size_t>(local_window, peer_window);
		window_cap = std::min<size_t>(window_cap, static_cast<size_t>(std::floor(cwnd)));
		window_cap = std::min<size_t>(window_cap, sack_bits);
		return window_cap;
	}

}  // namespace rtp
