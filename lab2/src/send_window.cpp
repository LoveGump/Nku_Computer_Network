#include "send_window.h"

#include <algorithm>
#include <cmath>

namespace rtp {
	using std::size_t;

	SendWindow::SendWindow() : total_segments_(0), base_seq_(1), next_seq_(1) {}

	void SendWindow::initialize(uint64_t file_size_bytes) {
		total_segments_ = static_cast<uint32_t>((file_size_bytes + MAX_PAYLOAD - 1) / MAX_PAYLOAD);
		segments_.clear();
		base_seq_ = 1;
		next_seq_ = 1;
	}

	void SendWindow::mark_acked(uint32_t seq) {
		if (seq == 0 || seq > total_segments_) {
			// 序号无效，忽略
			return;
		}
		auto it = segments_.find(seq);
		if (it == segments_.end()) {
			return;
		}
		auto& seg = it->second;
		if (seg.acked) {
			return;
		}
		seg.acked = true;
		seg.last_sack_retx = 0;
		seg.data.clear();
		seg.data_loaded = false;
	}

	SendWindow::SegmentInfo& SendWindow::get_segment(uint32_t seq) {
		auto it = segments_.find(seq);
		if (it != segments_.end()) {
			return it->second;
		}
		auto [ins_it, _] = segments_.emplace(seq, SegmentInfo{});
		return ins_it->second;
	}

	void SendWindow::set_base_seq(uint32_t seq) {
		base_seq_ = seq;
		for (auto it = segments_.begin(); it != segments_.end();) {
			if (it->first < base_seq_) {
				it = segments_.erase(it);
			} else {
				++it;
			}
		}
	}

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
		while (base_seq_ <= total_segments_) {
			auto it = segments_.find(base_seq_);
			if (it == segments_.end()) {
				return;
			}
			if (!it->second.acked) {
				return;
			}
			segments_.erase(it);
			++base_seq_;
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
