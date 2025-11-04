
#include <winsock2.h>
#include <string>
#include <thread>
#include <iostream>

#include "chat_server.h"

int main(int argc, char** argv) {
    // 初始化 Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        // 初始化失败
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // 解析端口
    uint16_t port = chatproto::DEFAULT_PORT;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    // 创建聊天服务器
    ChatServer server;
    if (!server.start(port)) {
        // 启动服务器失败
        std::fprintf(stderr, "Failed to start server on port %u\n", port);
        WSACleanup();
        return 1;
    }

    std::printf("Chat server listening on port %u\n", port);
    std::printf("Type 'quit' + Enter to stop.\n");

    std::thread quitThread([&]{
        // 等待用户输入 "quit" 命令以停止服务器
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") break;
        }
        server.stop();
        std::cout<< "serverstop()"<<std::endl;
    });

    if (quitThread.joinable()) quitThread.join();

    WSACleanup();
    std::printf("Server stopped.\n");
    return 0;
}
