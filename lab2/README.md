# 基于 UDP 的可靠传输协议

本项目在用户态基于 UDP 实现简化版的 TCP 式可靠传输：支持连接建立/关闭、校验和检错、选择确认的流水线重传、固定大小的流量控制窗口，以及 Reno 拥塞控制。

## 目录结构
- `src/rtp.h` / `src/rtp.cpp`：报文格式、校验和、序列化解析、跨平台 socket 辅助。
- `src/sender.h` / `src/sender.cpp`：`ReliableSender`，完成三次握手、流水线发送、SACK 处理、Reno 拥塞控制、FIN 结束和吞吐统计。
- `src/receiver.h` / `src/receiver.cpp`：`ReliableReceiver`，缓存乱序数据、通告窗口与 SACK、FIN 结束和吞吐统计。
- `src/sender_main.cpp`、`src/receiver_main.cpp`：命令行入口。
- `CMakeLists.txt`：CMake 构建脚本。

## 协议设计
- **报文字段**（网络字节序，紧凑对齐）：
  - `seq`：32 位分段序号（每个负载块一个）。
  - `ack`：32 位累计确认，表示期望的下一个有序序号。
  - `wnd`：16 位接收窗口大小（以段为单位，双方固定相同大小）。
  - `len`：16 位负载长度（字节）。
  - `flags`：16 位标志位，`SYN|ACK|FIN|DATA`。
  - `sack_mask`：32 位 SACK 位图，反馈 `ack` 之后最多 32 个段的选择性确认（位 i 确认 `ack+1+i`）。
  - `checksum`：16 位 1’s 补码校验和，覆盖首部+负载，计算时该字段置 0。
- **连接管理**：
  - 建立：`SYN` -> `SYN|ACK` -> `ACK`，数据序号从 1 开始。
  - 关闭：发送端发 `FIN`，接收端回 `FIN|ACK`，发送端回最终 `ACK`。
- **可靠性与重传**：
  - 校验和错误直接丢弃。
  - 接收端在窗口 `[expected, expected+wnd)` 内缓存乱序段，连续数据写入文件并推进 `expected`。
  - SACK：累计 ACK + 位图，发送端据此提前标记已达段。
  - 重传：按段超时重传并触发拥塞退避；三重冗余 ACK 触发快速重传。
- **流量控制**：固定大小窗口，发送端在任意时刻的未确认段数受 `min(对端通告窗, 本地窗, floor(cwnd))` 限制。
- **拥塞控制（Reno）**：
  - 变量：拥塞窗口 `cwnd`（段），慢启动阈值 `ssthresh`。
  - 慢启动：`cwnd < ssthresh` 时每个 ACK 线性加 1。
  - 拥塞避免：`cwnd >= ssthresh` 时每个 ACK 加 `1/cwnd`。
  - 超时：`ssthresh = cwnd/2`，`cwnd = 1`，退出快速恢复。
  - 三重冗余 ACK：`ssthresh = cwnd/2`，`cwnd = ssthresh + 3`，立即重传基段；后续冗余 ACK 线性膨胀 `cwnd`；首个新 ACK 退出快速恢复并设 `cwnd = ssthresh`。

## 实现说明
- 发送端：读取文件按 `MAX_PAYLOAD` 分段，跟踪发送/确认/时间戳；基于滑动窗口 + Reno 控制发包，处理 SACK，超时或三重 ACK 重传；FIN 结束后输出耗时与平均吞吐。
- 接收端：等待 SYN 建链，锁定对端；窗口内缓存乱序段，连续段落地并前移 `expected`，每个数据包回 ACK+SACK 与窗口；收到 FIN 回 `FIN|ACK` 并打印吞吐。
- 校验和：16 位 1’s 补码，完整报文计算结果应为 0。
- 跨平台：POSIX 使用 BSD sockets，Windows 使用 Winsock（`init_socket_lib` 内调用 `WSAStartup`）。

## 构建
推荐 CMake（C++17）：
```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_SH="CMAKE_SH-NOTFOUND"
cmake --build build --config Release
```
生成：`build/Release/sender(.exe)`、`build/Release/receiver(.exe)`（单配置生成器则在 `build/` 下）。

直接编译（无需 CMake）：
- POSIX：
  ```bash
  g++ -std=c++17 -O2 -o sender src/sender_main.cpp src/sender.cpp src/rtp.cpp
  g++ -std=c++17 -O2 -o receiver src/receiver_main.cpp src/receiver.cpp src/rtp.cpp
  ```
- Windows（MinGW/MSVC）：
  ```bash
  g++ -std=c++17 -O2 -o sender.exe src/sender_main.cpp src/sender.cpp src/rtp.cpp -lws2_32
  g++ -std=c++17 -O2 -o receiver.exe src/receiver_main.cpp src/receiver.cpp src/rtp.cpp -lws2_32
  ```

## 运行与测试
1. 启动接收端：`./receiver <listen_port> <output_file> <window_size>`
2. 启动发送端：`./sender <receiver_ip> <receiver_port> <input_file> <window_size>`
3. 结束时双方输出耗时和平均吞吐率。调整 `window_size` 可观察流控影响；在链路上引入丢包/延迟（如 `tc netem`）可观察不同丢包率下的性能变化。

### 预期表现
- 窗口更大时吞吐提升，直到丢包触发 Reno 回退。
- 较高丢包率下，SACK + 快速重传比仅依赖超时恢复更快。
- 数据单向传输，控制报文双向交换。

