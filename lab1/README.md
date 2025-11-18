# 基于流式套接字（TCP）的多人聊天程序（C++/Win32）

本项目实现了一个支持中文与英文的多人聊天系统，包括：
- 服务端（控制台），多线程，基于 Winsock2，原生 socket API（无 CSocket 等封装）
- 客户端（Win32 原生 GUI），基本聊天界面、连接/断开、发送消息
- 简单且健壮的帧协议，UTF-8 编码

## 协议说明

所有消息使用 TCP 传输，采用“帧协议”：
- 帧结构：`[1 字节 类型][4 字节 负载长度（大端）][N 字节 负载]`
- 编码：所有字符串均为 UTF-8 编码；长度不超过 64 KiB
- 消息类型：
  - `0x01 HELLO`（C->S）：负载为 UTF-8 昵称
  - `0x02 CHAT`（C->S）：负载为 UTF-8 聊天文本
  - `0x11 USER_JOIN`（S->C）：负载为 UTF-8 昵称（某用户加入）
  - `0x12 USER_LEAVE`（S->C）：负载为 UTF-8 昵称（某用户离开）
  - `0x13 SERVER_BROADCAST`（S->C）：负载为 `from + '\n' + text`（均为 UTF-8），用于服务器广播聊天消息

时序与约束：
- 客户端连接后必须先发送 `HELLO`（携带昵称），服务端收到后才算入群，并向所有客户端广播 `USER_JOIN`
- 客户端发送 `CHAT`，服务端将其转换为 `SERVER_BROADCAST` 广播（附带发送者昵称）
- 客户端断开或异常，服务端向所有客户端广播 `USER_LEAVE`
- 超长负载（>64 KiB）或非法类型的帧将导致连接关闭

错误处理与健壮性：
- 采用长度前缀，解决黏包/半包问题；读写均使用 `recvAll/sendAll` 循环保障完整性
- 对异常断开、发送失败的客户端，服务端会清理并通知其他客户端

## 目录结构

```
.
├─ CMakeLists.txt            # 根 CMake
├─ src
│  └─ common
│     └─ protocol.h          # 协议与收发工具（头文件实现）
├─ server
│  ├─ CMakeLists.txt
│  └─ main.cpp               # 多线程服务端
└─ client
   ├─ CMakeLists.txt
   └─ main.cpp               # Win32 GUI 客户端
```

## 构建（仅 Windows + MSVC）

前置：
- Windows 10/11
- CMake 3.20+
- Microsoft Visual C++ (VS 2019/2022)

示例（PowerShell）：
```powershell
# 在项目根目录执行（VS 2022，x64）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
构建产物位于 `build/bin/`：`chat_server(.exe)` 与 `chat_client(.exe)`。

## 运行

1. 启动服务端（可指定端口，默认 5000）：
```powershell
build\bin\chat_server.exe 5000
```
控制台输入 `quit` 回车可优雅退出。

2. 启动客户端：
- 运行 `build\bin\chat_client.exe`
- 填写“服务器地址”（默认 127.0.0.1）、“端口”（默认 5000）、“昵称”（默认 User）
- 点击“连接”，下方为聊天记录，多行只读；底部输入消息，点击“发送”或按 Enter 发送
- 点击“断开”可正常退出连接；关闭窗口也会自动断开

中文支持说明：
- 协议统一使用 UTF-8；客户端内部使用 UTF-16（Win32 宽字符），通过 `WideCharToMultiByte/MultiByteToWideChar` 转换
- 界面使用系统默认字体，支持显示中文

## 线程与并发

- 服务端：主线程 `accept`，每个客户端独立线程收包与处理；广播时使用互斥锁保护客户端列表
- 客户端：网络收包线程使用 `PostMessage` 将文本传回 UI 线程拼接显示（避免跨线程直接操作控件）

## 正常退出

- 客户端：点击“断开”或关闭窗口，均会 `shutdown/ closesocket`，并等待接收线程结束
- 服务端：控制台输入 `quit`，会关闭监听 socket 并清理全部客户端连接

## 后续可选改进

- 私聊与在线列表同步（新增帧类型）
- 心跳保活（PING/PONG）
- 历史消息持久化与消息时间戳
- 更丰富的 GUI（RichEdit、表情、换行发送快捷键等）
