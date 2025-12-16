#include <iostream>

#include "receiver.h"

using namespace rtp;
using namespace std;

static void usage(const char* prog) {
    cout << "Usage: " << prog << " <listen_port> <output_file> <window_size>\n";
}

int main(int argc, char** argv) {
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }
    uint16_t port = static_cast<uint16_t>(stoi(argv[1]));
    string out_path = argv[2];
    uint16_t window_size = static_cast<uint16_t>(stoi(argv[3]));

    ReliableReceiver receiver(port, out_path, window_size);
    return receiver.run();
}
