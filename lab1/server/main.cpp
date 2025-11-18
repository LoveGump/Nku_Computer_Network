
#include <winsock2.h>

#include <iostream>
#include <string>
#include <thread>

#include "chat_server.h"

int main(int argc, char **argv) {
    // 初始化 Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        // 初始化失败
        std::cerr << "WSAStartup failed" << std::endl;
        ;
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
        // 启动服务器失败（C++ 风格输出）
        std::cerr << "Failed to start server on port " << port << std::endl;
        ;
        WSACleanup();
        return 1;
    }

    std::cout << "Chat server listening on port " << port << std::endl;
    std::cout << "Type 'quit' + Enter to stop." << std::endl;

    std::thread quitThread([&] {
        // 等待用户输入 "quit" 命令以停止服务器
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") break;
        }
        server.stop();
        std::cout << "serverstop()" << std::endl;
    });

    if (quitThread.joinable()) quitThread.join();

    WSACleanup();
    std::cout << "Server stopped." << std::endl;
    return 0;
}
