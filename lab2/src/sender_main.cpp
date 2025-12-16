#include "sender.h"

#include <iostream>

using namespace rtp;

static void usage(const char *prog)
{
    std::cout << "Usage: " << prog << " <receiver_ip> <receiver_port> <input_file> <window_size> [local_port]\n";
    std::cout << "  local_port: Optional. Bind to specific local port (default: auto-assign)\n";
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        usage(argv[0]);
        return 1;
    }
    std::string ip = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    std::string file_path = argv[3];
    uint16_t window_size = static_cast<uint16_t>(std::stoi(argv[4]));
    uint16_t local_port = 0;
    if (argc >= 6)
    {
        local_port = static_cast<uint16_t>(std::stoi(argv[5]));
    }

    ReliableSender sender(ip, port, file_path, window_size, local_port);
    return sender.run();
}
