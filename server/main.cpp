
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <iostream>
#include <algorithm>

#include "common/protocol.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace chatproto;

struct ClientInfo {
    SOCKET sock{INVALID_SOCKET};
    std::string nickname;
};

std::vector<ClientInfo*> g_clients;
std::mutex g_clients_mtx;
std::atomic<bool> g_running{true};

void broadcast(MsgType type, const std::string& payload, ClientInfo* exclude = nullptr) {
    std::lock_guard<std::mutex> lock(g_clients_mtx);
    for (auto it = g_clients.begin(); it != g_clients.end(); ) {
        ClientInfo* c = *it;
        if (exclude && c == exclude) { ++it; continue; }
        if (!sendFrame(c->sock, type, payload)) {
            // remove dead client
            closesocket(c->sock);
            it = g_clients.erase(it);
            delete c;
        } else {
            ++it;
        }
    }
}

void handle_client(ClientInfo* client) {
    // Expect HELLO
    MsgType t; std::string p;
    if (!recvFrame(client->sock, t, p) || t != MsgType::HELLO) {
        closesocket(client->sock);
        delete client;
        return;
    }
    client->nickname = p; // already UTF-8

    // Announce join
    std::string joinName = client->nickname;
    broadcast(MsgType::USER_JOIN, joinName, nullptr);

    // Main loop
    while (g_running.load()) {
        MsgType type; std::string payload;
        if (!recvFrame(client->sock, type, payload)) break; // disconnect
        if (type == MsgType::CHAT) {
            // Build broadcast payload: from + '\n' + message
            std::string combined = client->nickname;
            combined.push_back('\n');
            combined += payload;
            broadcast(MsgType::SERVER_BROADCAST, combined, nullptr);
        } else if (type == MsgType::BYE) {
            // Graceful client-initiated disconnect
            if (!payload.empty()) client->nickname = payload;
            break;
        } else {
            // ignore unknown
        }
    }

    // leave
    broadcast(MsgType::USER_LEAVE, client->nickname, client);

    // remove client
    {
        std::lock_guard<std::mutex> lock(g_clients_mtx);
        auto it = std::find(g_clients.begin(), g_clients.end(), client);
        if (it != g_clients.end()) g_clients.erase(it);
    }
    closesocket(client->sock);
    delete client;
}

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

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::fprintf(stderr, "socket() failed\n");
        WSACleanup();
        return 1;
    }

    u_long yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::fprintf(stderr, "bind() failed\n");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        std::fprintf(stderr, "listen() failed\n");
        closesocket(listenSock);
        WSACleanup();
        return 1;
    }

    std::printf("Chat server listening on port %u\n", port);
    std::printf("Type 'quit' + Enter to stop.\n");

    // Thread to read stdin for quit
    std::thread quitThread([&]{
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") {
                g_running.store(false);
                // close listen socket to unblock accept
                closesocket(listenSock);
                break;
            }
        }
    });

    while (g_running.load()) {
        sockaddr_in caddr{}; int clen = sizeof(caddr);
        SOCKET cs = accept(listenSock, (sockaddr*)&caddr, &clen);
        if (cs == INVALID_SOCKET) {
            if (!g_running.load()) break; // shutting down
            continue; // spurious
        }
        auto* ci = new ClientInfo();
        ci->sock = cs;
        {
            std::lock_guard<std::mutex> lock(g_clients_mtx);
            g_clients.push_back(ci);
        }
        std::thread th(handle_client, ci);
        th.detach();
    }

    // Cleanup: close all clients
    {
        std::lock_guard<std::mutex> lock(g_clients_mtx);
        for (auto* c : g_clients) {
            closesocket(c->sock);
            delete c;
        }
        g_clients.clear();
    }

    if (quitThread.joinable()) quitThread.join();

    closesocket(listenSock);
    WSACleanup();
    std::printf("Server stopped.\n");
    return 0;
}
