// 接收缓冲区实现
#include "receive_buffer.h"

#include <iostream>

namespace rtp {
	using std::vector;
	ReceiveBuffer::ReceiveBuffer(uint16_t window_size) : expected_seq_(0), window_size_(window_size) {}

	bool ReceiveBuffer::add_segment(uint32_t seq, const vector<uint8_t>& data) {
		// 检查是否已存在
		if (buffer_.find(seq) != buffer_.end()) {
			return false;
		}

		buffer_.emplace(seq, data);
		return true;
	}

	vector<vector<uint8_t>> ReceiveBuffer::extract_continuous_segments() {
		vector<vector<uint8_t>> result;

		// 从expected_seq开始，提取所有连续的段
		while (true) {
			auto it = buffer_.find(expected_seq_);
			if (it == buffer_.end()) {
				break;	// 遇到缺口，停止提取
			}

			// 提取该段数据
			result.push_back(std::move(it->second));
			// 从缓冲区移除该段
			buffer_.erase(it);
			expected_seq_++;  // 推进期望序号
		}

		return result;
	}

	uint32_t ReceiveBuffer::build_sack_mask() const {
		uint32_t mask = 0;
		// 检查expected_seq之后的32个段是否已到达
		for (uint32_t i = 0; i < 32; ++i) {
			uint32_t seq = expected_seq_ + 1 + i;
			if (buffer_.find(seq) != buffer_.end()) {
				mask |= (1u << i);	// 该段已到达，置位
			}
		}
		return mask;
	}

	bool ReceiveBuffer::is_in_window(uint32_t seq) const {
		return seq >= expected_seq_ && seq < expected_seq_ + window_size_;
	}

}  // namespace rtp
