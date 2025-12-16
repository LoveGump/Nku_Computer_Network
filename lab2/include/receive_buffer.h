#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace rtp {
    using std::map;
    using std::vector;

    /**
     * 接收缓冲区管理
     * 负责乱序段缓存、连续段提取、SACK掩码生成
     */
    class ReceiveBuffer {
       public:
        explicit ReceiveBuffer(uint16_t window_size);

        // 添加接收到的段
        // 返回值：true表示新段，false表示重复
        // 重复段不会被添加到缓冲区
        bool add_segment(uint32_t seq, const vector<uint8_t>& data);

        // 提取所有连续的段（从expected_seq开始）
        // 返回：可以写入文件的连续数据列表
        // 自动推进expected_seq到下一个缺失的段
        vector<vector<uint8_t>> extract_continuous_segments();

        // 构建SACK掩码（32位，标记expected_seq+1起的32个段）
        // 位i=1表示序号expected_seq+1+i的段已到达
        uint32_t build_sack_mask() const;

        // 获取期望序号
        uint32_t get_expected_seq() const { return expected_seq_; }

        // 设置期望序号（握手后初始化）
        void set_expected_seq(uint32_t seq) { expected_seq_ = seq; }

        // 检查序号是否在窗口内
        bool is_in_window(uint32_t seq) const;

        // 检查是否为重复包
        bool is_duplicate(uint32_t seq) const;

        // 获取窗口大小
        uint16_t get_window_size() const { return window_size_; }

       private:
        uint32_t expected_seq_;                  // 下一个期望的序号
        uint16_t window_size_;                   // 接收窗口大小
        map<uint32_t, vector<uint8_t>> buffer_;  // 乱序段缓冲区
    };

}  // namespace rtp
