#include <iostream>

#include "sender.h"

using namespace rtp;
using namespace std;
// 显示用法信息
static void usage(const char* prog) {
    cout << "Usage: " << prog
         << " <receiver_ip> <receiver_port> <input_file> <window_size> "
            "[local_port]\n";
    cout << "  local_port: Optional. Bind to specific local port (default: "
            "auto-assign)\n";
}

int main(int argc, char** argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }
    string ip = argv[1];
    uint16_t port = static_cast<uint16_t>(stoi(argv[2]));
    string file_path = argv[3];
    uint16_t window_size = static_cast<uint16_t>(stoi(argv[4]));
    uint16_t local_port = 0;
    if (argc >= 6) {
        local_port = static_cast<uint16_t>(stoi(argv[5]));
    }

    // 创建并运行可靠发送器
    ReliableSender sender(ip, port, file_path, window_size, local_port);
    return sender.run();
}
