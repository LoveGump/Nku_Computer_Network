#include <iostream>

#include "receiver.h"
#include "utils/logger.h"

using namespace rtp;
using namespace std;

static constexpr uint16_t DEFAULT_WINDOW_SIZE = 32;

static void usage(const char* prog) {
	cout << "Usage: " << prog << " <listen_port> <output_file> [window_size]" << endl;
	cout << "  window_size: Optional. Defaults to " << DEFAULT_WINDOW_SIZE << endl;
}

int main(int argc, char** argv) {
	try {
		Logger::instance().init("logs/receiver.log", true);
	} catch (const std::exception& ex) {
		cerr << "Logger init failed: " << ex.what() << endl;
	}

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}
	uint16_t port = static_cast<uint16_t>(stoi(argv[1]));
	string out_path = argv[2];
	uint16_t window_size = DEFAULT_WINDOW_SIZE;
	if (argc >= 4) {
		window_size = static_cast<uint16_t>(stoi(argv[3]));
	}

	ReliableReceiver receiver(port, out_path, window_size);
	return receiver.run();
}
