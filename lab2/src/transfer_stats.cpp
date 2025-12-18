#include "transfer_stats.h"

namespace rtp {
	using std::cout;
	using std::endl;
	using std::size_t;

	TransferStats::TransferStats()
		: retransmit_count_(0), timeout_count_(0), fast_retransmit_count_(0), start_time_(0), end_time_(0) {}

	double TransferStats::get_elapsed_seconds() const {
		if (end_time_ > start_time_) {
			return (end_time_ - start_time_) / 1000.0;
		}
		return 0.0;
	}

	double TransferStats::get_throughput(size_t bytes) const {
		double elapsed = get_elapsed_seconds();
		if (elapsed > 0) {
			return static_cast<double>(bytes) / elapsed / 1024.0 / 1024.0;
		}
		return 0.0;
	}

	double TransferStats::get_loss_rate(uint32_t total_segments) const {
		if (total_segments > 0) {
			return (retransmit_count_ * 100.0) / total_segments;
		}
		return 0.0;
	}

	void TransferStats::print_sender_stats(size_t file_size, uint32_t total_segments, double cwnd,
										   double ssthresh) const {
		cout << "[INFO] Transfer completed" << endl;
		cout << "[INFO] Final cwnd: " << cwnd << ", Final ssthresh: " << ssthresh
			 << endl;  // 打印最终拥塞窗口和慢启动阈值
		cout << "[STATS] Total retransmits: " << retransmit_count_ << " (Timeout: " << timeout_count_
			 << ", Fast retransmit: " << fast_retransmit_count_ << ")" << endl;
		cout << "[STATS] Packet loss rate: " << get_loss_rate(total_segments) << "%" << endl;
		cout << "Sent " << file_size << " bytes in " << get_elapsed_seconds() << " s, avg throughput "
			 << get_throughput(file_size) << " MiB/s" << endl;
	}

	void TransferStats::print_receiver_stats(size_t bytes_received, uint32_t total_packets, uint32_t out_of_order,
											 uint32_t duplicates) const {
		cout << "[INFO] Transfer completed" << endl;
		cout << "[STATS] Total packets received: " << total_packets << endl;
		cout << "[STATS] Out-of-order packets: " << out_of_order << endl;
		cout << "[STATS] Duplicate packets: " << duplicates << endl;
		cout << "Received " << bytes_received << " bytes in " << get_elapsed_seconds() << " s, avg throughput "
			 << get_throughput(bytes_received) << " MiB/s" << endl;
	}

}  // namespace rtp
