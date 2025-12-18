#include <iostream>

#include "receiver.h"
#include "utils/logger.h"

using namespace rtp;
using namespace std;

static void usage(const char* prog) {
	cout << "Usage: " << prog << " <listen_port> <output_file> <window_size>" << endl;
}

int main(int argc, char** argv) {
	try {
		Logger::instance().init("logs/receiver.log", false);
	} catch (const std::exception& ex) {
		cerr << "Logger init failed: " << ex.what() << endl;
	}

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
