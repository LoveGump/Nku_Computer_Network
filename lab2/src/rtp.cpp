#include "rtp.h"

#include <cstring>
#include <iostream>
#include <random>

namespace rtp {
	using std::cerr;
	using std::size_t;
	using std::vector;
	namespace {
		// FNV-1a 哈希函数实现
		uint32_t fnv1a(const uint8_t* data, size_t len) {
			constexpr uint32_t FNV_PRIME = 16777619u;  // FNV素数
			uint32_t hash = 2166136261u;			   // FNV偏移基数
			for (size_t i = 0; i < len; ++i) {
				hash ^= static_cast<uint32_t>(data[i]);
				hash *= FNV_PRIME;
			}
			return hash;
		}

		uint64_t generate_salt_once() {
			std::random_device rd;
			return (uint64_t(rd()) << 32) ^ rd();
		}
		uint64_t secret_salt() {
			static const uint64_t salt = generate_salt_once();
			return salt;
		}
	}  // namespace

	uint16_t compute_checksum(const uint8_t* data, size_t len) {
		uint32_t sum = 0;  // 32 位累加器
		size_t i = 0;
		while (i + 1 < len) {
			// 16 位字加法
			uint16_t word = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
			sum += word;
			// 处理溢出
			sum = (sum & 0xFFFF) + (sum >> 16);
			i += 2;
		}
		if (i < len) {
			// 剩余一个字节，补0处理
			uint16_t word = static_cast<uint16_t>(data[i] << 8);
			sum += word;
			sum = (sum & 0xFFFF) + (sum >> 16);
		}
		// 取反得到校验和
		return static_cast<uint16_t>(~sum);
	}

	vector<uint8_t> serialize_packet(const PacketHeader& header, const vector<uint8_t>& payload) {
		PacketHeader net = header;
		// 转换为大端序
		net.seq = htonl(header.seq);
		net.ack = htonl(header.ack);
		net.wnd = htons(header.wnd);
		net.len = htons(header.len);
		net.flags = htons(header.flags);
		net.sack_mask = htonl(header.sack_mask);
		// 校验和字段置0以便计算校验和
		net.checksum = 0;
		vector<uint8_t> buffer(sizeof(PacketHeader) + payload.size());	// 分配缓冲区
		memcpy(buffer.data(), &net, sizeof(PacketHeader));				// 复制头部

		// 如果有有效载荷，复制有效载荷
		if (!payload.empty()) {
			memcpy(buffer.data() + sizeof(PacketHeader), payload.data(), payload.size());
		}
		// 计算校验和并填入头部
		uint16_t cs = compute_checksum(buffer.data(), buffer.size());
		auto* hdr = reinterpret_cast<PacketHeader*>(buffer.data());
		hdr->checksum = htons(cs);
		return buffer;
	}

	bool parse_packet(const uint8_t* data, size_t len, Packet& out) {
		if (len < sizeof(PacketHeader)) {
			// 数据包长度小于头部长度，非法包
			return false;
		}
		PacketHeader net{};
		memcpy(&net, data, sizeof(PacketHeader));
		if (compute_checksum(data, len) != 0) {
			// 计算的校验和不为0，校验失败
			return false;
		}

		// 转为小端序
		out.header.seq = ntohl(net.seq);
		out.header.ack = ntohl(net.ack);
		out.header.wnd = ntohs(net.wnd);
		out.header.len = ntohs(net.len);
		out.header.flags = ntohs(net.flags);
		out.header.sack_mask = ntohl(net.sack_mask);
		out.header.checksum = ntohs(net.checksum);

		if (out.header.len + sizeof(PacketHeader) != len) {
			// 长度字段与实际长度不符，非法包
			return false;
		}

		// 提取有效载荷
		out.payload.clear();
		if (out.header.len > 0) {
			// 复制有效载荷数据
			// 从data偏移 sizeof(PacketHeader)开始，到偏移len结束
			out.payload.insert(out.payload.end(), data + sizeof(PacketHeader), data + len);
		}
		return true;
	}

	uint32_t generate_isn(const sockaddr_in& local, const sockaddr_in& remote) {
		// 模仿RFC 6528基于地址和端口的哈希生成ISN
		uint8_t tuple_buf[12] = {0};
		memcpy(tuple_buf, &local.sin_addr.s_addr,
			   sizeof(local.sin_addr.s_addr));							 // 4 本地的IP地址
		memcpy(tuple_buf + 4, &local.sin_port, sizeof(local.sin_port));	 // 2 本地的端口号
		memcpy(tuple_buf + 6, &remote.sin_addr.s_addr, sizeof(remote.sin_addr.s_addr));
		memcpy(tuple_buf + 10, &remote.sin_port, sizeof(remote.sin_port));

		uint64_t salt = secret_salt();	// 获取全局盐值
		uint8_t salt_buf[8] = {0};		// 8 字节盐值缓冲区
		memcpy(salt_buf, &salt, sizeof(salt_buf));
		uint32_t hash = fnv1a(tuple_buf, sizeof(tuple_buf));
		hash ^= fnv1a(salt_buf, sizeof(salt_buf));

		uint32_t counter = static_cast<uint32_t>(now_ms());
		return hash + counter;
	}

	uint64_t now_ms() {
		using namespace std::chrono;
		return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	string addr_to_string(const sockaddr_in& addr) {
		// 转换IP地址和端口为字符串形式inet_ntoa：将网络字节序的IP地址转换为点分十进制字符串
		char* ip_str = inet_ntoa(addr.sin_addr);
		return string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
	}
}  // namespace rtp
