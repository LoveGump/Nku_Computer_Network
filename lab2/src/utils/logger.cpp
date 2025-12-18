#include "utils/logger.h"

#include <cerrno>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace rtp {

	Logger& Logger::instance() {
		static Logger inst;
		return inst;
	}

	namespace {

		bool make_dir(const std::string& dir) {
			if (dir.empty()) {
				return true;
			}

#ifdef _WIN32
			int rc = _mkdir(dir.c_str());
#else
			int rc = mkdir(dir.c_str(), 0755);
#endif

			return rc == 0 || errno == EEXIST;
		}

		bool ensure_parent_directory(const std::string& file_path) {
			const size_t pos = file_path.find_last_of("/\\");
			if (pos == std::string::npos) {
				return true;
			}

			std::string dir = file_path.substr(0, pos);

			// Create intermediate directories one-by-one to handle nested
			// paths.
			size_t start = 0;
			while (true) {
				size_t next_sep = dir.find_first_of("/\\", start);
				std::string chunk = dir.substr(0, next_sep);
				if (!make_dir(chunk)) {
					return false;
				}
				if (next_sep == std::string::npos) {
					break;
				}
				start = next_sep + 1;
			}

			return true;
		}

	}  // namespace

	void Logger::init(const std::string& file_path, bool also_console) {
		if (initialized_) {
			return;
		}

		if (!ensure_parent_directory(file_path)) {
			throw std::runtime_error("Failed to create log directory for: " + file_path);
		}

		file_.open(file_path, std::ios::out | std::ios::app);
		if (!file_) {
			throw std::runtime_error("Failed to open log file: " + file_path);
		}

		old_cout_ = std::cout.rdbuf();
		old_cerr_ = std::cerr.rdbuf();

		tee_cout_.reset(file_.rdbuf(), also_console ? old_cout_ : nullptr);
		tee_cerr_.reset(file_.rdbuf(), also_console ? old_cerr_ : nullptr);

		std::cout.rdbuf(&tee_cout_);
		std::cerr.rdbuf(&tee_cerr_);

		initialized_ = true;
	}

	void Logger::shutdown() {
		if (!initialized_) {
			return;
		}

		std::cout.rdbuf(old_cout_);
		std::cerr.rdbuf(old_cerr_);

		file_.flush();
		file_.close();
		initialized_ = false;
	}

	Logger::~Logger() { shutdown(); }

}  // namespace rtp
