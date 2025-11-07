    #pragma once

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <string>
    #include <vector>
    #include <memory>
    #include <mutex>
    #include <thread>
    #include <atomic>

    #include "common/protocol.h"

    class ClientSession;
    /**
     * 多线程 TCP 聊天服务端类
     */
    class ChatServer {
    public:
        ChatServer() = default; // 默认构造函数
        ~ChatServer() { stop(); }

        bool start(uint16_t port);
        void stop();

        // 广播到所有客户端
        void broadcast(chatproto::MsgType type, const std::string& payload, ClientSession* exclude = nullptr);

    private:
        friend class ClientSession; // 允许会话通知服务器移除自身
        void acceptLoop(); // 接受连接循环
        void removeClient(ClientSession* c); // 移除客户端会话

    private:
        SOCKET listenSock_ { INVALID_SOCKET }; // 监听套接字
        std::thread acceptThread_; // 服务器接受线程
        std::vector<ClientSession*> clients_; // 活动客户端列表
        std::mutex clientsMtx_;     // 保护客户端列表的互斥锁
        std::atomic<bool> running_{false};  // 服务器运行状态
    };

    /**
     * 客户端会话类，处理单个客户端连接
     */
    class ClientSession {
    public:
        ClientSession(ChatServer* server, SOCKET s): server_(server), sock_(s) {}
        ~ClientSession();

        void start();
        // 从外部请求关闭：唤醒阻塞并安全关闭套接字
        void forceClose();

        SOCKET sock() const { return sock_.load(); }
        const std::string& nickname() const { return nickname_; }

    private:
        void run();

    private:
        ChatServer* server_{}; // 所属服务器指针
        std::atomic<SOCKET> sock_{ INVALID_SOCKET }; // 客户端套接字（原子，避免竞态）
        std::thread thread_; // 处理线程
        std::string nickname_;// 客户端昵称
    };
