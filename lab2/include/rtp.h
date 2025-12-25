// rtp.h
// Reliable Transport Protocol协议核心定义头文件
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;

inline bool socket_valid(socket_t s) { return s != INVALID_SOCKET; }

namespace rtp {
	using std::size_t;
	using std::string;
	using std::vector;

	constexpr size_t MAX_PAYLOAD = 1460;  // 最大有效载荷大小（与典型MTU匹配）
	constexpr uint16_t FLAG_SYN = 0x01;	  // SYN
	constexpr uint16_t FLAG_ACK = 0x02;	  // ACK
	constexpr uint16_t FLAG_FIN = 0x04;	  // FIN
	constexpr uint16_t FLAG_DATA = 0x08;  // 数据段
	constexpr uint16_t FLAG_RST = 0x10;	  // RST，复位段，用于异常终止连接

	constexpr int HANDSHAKE_TIMEOUT_MS = 8000;	// 握手超时时间（毫秒）
	constexpr int DATA_TIMEOUT_MS = 50000;		// 数据传输超时时间（毫秒）

#pragma pack(push, 1)
	struct PacketHeader {
		uint32_t seq;		 // 32 位发送序号
		uint32_t ack;		 // 32 位确认号
		uint16_t flags;		 // 16 位标志位
		uint16_t wnd;		 // 16 位接收窗口通告
		uint16_t checksum;	 // 16 位校验和
		uint16_t len;		 // 16 位有效载荷长度
		uint32_t sack_mask;	 // 32 位SACK掩码
	};
#pragma pack(pop)

	// 数据包结构体
	struct Packet {
		PacketHeader header{};
		vector<uint8_t> payload;
	};

	// 计算校验和
	uint16_t compute_checksum(const uint8_t* data, size_t len);

	// 修改为大端序，组装数据包
	vector<uint8_t> serialize_packet(const PacketHeader& header, const vector<uint8_t>& payload);

	// 解析数据包并检验校验和
	bool parse_packet(const uint8_t* data, size_t len, Packet& out);

	// 生成初始序号（ISN），基于本地和远程地址的哈希
	uint32_t generate_isn(const sockaddr_in& local, const sockaddr_in& remote);

	// 获取当前时间戳（毫秒）
	uint64_t now_ms();
	string addr_to_string(const sockaddr_in& addr);

	bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b);

}  // namespace rtp
