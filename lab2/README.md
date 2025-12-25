# 基于 UDP 的可靠传输协议（实验项目）

本项目在用户态基于 UDP 实现“类 TCP”的可靠单向文件传输：数据从 sender 流向 receiver，但通过 ACK/SACK、重传、窗口与 Reno 拥塞控制等机制保证最终文件字节一致。

## 目录结构

```text
.
├─ CMakeLists.txt
├─ README.md
├─ include/
│  ├─ rtp.h
│  ├─ sender.h
│  ├─ receiver.h
│  ├─ send_window.h
│  ├─ receive_buffer.h
│  ├─ congestion_control.h
│  ├─ transfer_stats.h
│  └─ utils/
│     └─ logger.h
├─ src/
│  ├─ sender_main.cpp
│  ├─ sender.cpp
│  ├─ receiver_main.cpp
│  ├─ receiver.cpp
│  ├─ rtp.cpp
│  ├─ send_window.cpp
│  ├─ receive_buffer.cpp
│  ├─ congestion_control.cpp
│  ├─ transfer_stats.cpp
│  └─ utils/
│     └─ logger.cpp
├─ report/                 (实验报告与绘图脚本)
├─ logs/                   (运行时自动创建并写入日志)
└─ build/                  (CMake 构建目录，构建后生成)
```

## 项目框架

整体结构是“两端程序 + 一组可复用协议组件”：

- `src/sender_main.cpp`：sender 入口，解析命令行参数并启动发送端
- `src/receiver_main.cpp`：receiver 入口，解析命令行参数并启动接收端
- `include/rtp.h` + `src/rtp.cpp`：协议基础设施
  - 报文头 `PacketHeader`/`Packet`、序列化/反序列化（网络字节序）
  - 16-bit 反码校验和
  - socket 初始化与工具函数（Windows 下自动 `WSAStartup`）
- `include/sender.h` + `src/sender.cpp`：发送端核心 `ReliableSender`
  - 三次握手、数据流水线发送、ACK/SACK 处理、超时与快速重传、四次挥手关闭
  - 零窗口探测（Persist Timer）
  - 传输统计与进度输出
- `include/receiver.h` + `src/receiver.cpp`：接收端核心 `ReliableReceiver`
  - 被动握手、窗口内乱序缓存、按序落盘、ACK+SACK 回包、关闭处理
  - 统计乱序/重复包数量与吞吐
- `include/send_window.h` + `src/send_window.cpp`：发送窗口（在途段管理、已确认推进、窗口容量计算）
- `include/receive_buffer.h` + `src/receive_buffer.cpp`：接收端乱序缓存与 SACK 位图生成
- `include/congestion_control.h` + `src/congestion_control.cpp`：Reno 拥塞控制状态机（`cwnd/ssthresh/dupAck`）
- `include/transfer_stats.h` + `src/transfer_stats.cpp`：耗时、吞吐、重传等统计输出
- `include/utils/logger.h` + `src/utils/logger.cpp`：日志重定向到 `logs/*.log`（自动创建目录）

## 构建方式

本项目使用 CMake ，默认会生成两个可执行文件：`sender` 与 `receiver`（见 `CMakeLists.txt`）。

在receiver_main.cpp和sender_main.cpp 可以选择日志是否输出到控制台，当前配置为输出到控制台，方便调试。如需关闭控制台输出，可将对应文件中 Logger 初始化的第二个参数改为 false 。

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_SH="CMAKE_SH-NOTFOUND"
cmake --build build --config Release
```

产物位置`build/sender.exe`、`build/receiver.exe`

## 运行方式

先启动接收端，再启动发送端。

### 1）启动接收端

```bash
./build/receiver.exe <listen_port> <output_file> [window_size]
```

- `listen_port`：监听端口，例如 `8000`
- `output_file`：接收端输出文件路径
- `window_size`：可选，默认 `32`（建议不超过 32，与 SACK 位图宽度一致）

示例：
```bash
./build/receiver.exe 8000 out.bin 32
```
运行后等待发送端连接。
### 2）启动发送端

```bash
./build/sender.exe <receiver_ip> <receiver_port> <input_file> <window_size> [local_port]
```

- `receiver_ip`/`receiver_port`：接收端 IP 与端口
- `input_file`：发送端读取的输入文件路径
- `window_size`：发送窗口大小（建议与接收端一致，且不超过 32）
- `local_port`：可选，绑定本地端口（默认 `9000`）

示例（本机回环）：
```bash
./build/sender.exe 127.0.0.1 8000 .\2.jpg 32
```

运行结束后：
- receiver 侧会生成 `output_file`
- sender/receiver 默认把日志写到 `logs/sender.log`、`logs/receiver.log`

## 在不可靠网络下运行

在 Windows 上可用 clumsy 对 UDP 注入丢包/延迟；
