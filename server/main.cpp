
#include <winsock2.h>
#include <string>
#include <thread>
#include <iostream>

#include "chat_server.h"

int main(int argc, char** argv) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    uint16_t port = chatproto::DEFAULT_PORT;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    ChatServer server;
    if (!server.start(port)) {
        std::fprintf(stderr, "Failed to start server on port %u\n", port);
        WSACleanup();
        return 1;
    }

    std::printf("Chat server listening on port %u\n", port);
    std::printf("Type 'quit' + Enter to stop.\n");

    std::thread quitThread([&]{
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") break;
        }
        server.stop();
    });

    if (quitThread.joinable()) quitThread.join();

    WSACleanup();
    std::printf("Server stopped.\n");
    return 0;
}
