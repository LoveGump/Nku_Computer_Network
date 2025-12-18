#pragma once

#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>

namespace rtp {

	// Simple tee buffer that mirrors output to a log file buffer and
	// (optionally) the original console buffer.
	class TeeBuf : public std::streambuf {
	   public:
		TeeBuf() = default;
		TeeBuf(std::streambuf* file_buf, std::streambuf* console_buf) { reset(file_buf, console_buf); }

		void reset(std::streambuf* file_buf, std::streambuf* console_buf) {
			file_buf_ = file_buf;
			console_buf_ = console_buf;
		}

	   protected:
		int overflow(int ch) override {
			if (ch == traits_type::eof()) {
				return traits_type::not_eof(ch);
			}
			int r1 = file_buf_ ? file_buf_->sputc(static_cast<char>(ch)) : ch;
			int r2 = console_buf_ ? console_buf_->sputc(static_cast<char>(ch)) : ch;
			return (r1 == traits_type::eof() || r2 == traits_type::eof()) ? traits_type::eof() : ch;
		}

		int sync() override {
			int r1 = file_buf_ ? file_buf_->pubsync() : 0;
			int r2 = console_buf_ ? console_buf_->pubsync() : 0;
			return (r1 == 0 && r2 == 0) ? 0 : -1;
		}

	   private:
		std::streambuf* file_buf_{nullptr};
		std::streambuf* console_buf_{nullptr};
	};

	class Logger {
	   public:
		static Logger& instance();

		// Initialize logging. When also_console is true, logs are mirrored to
		// console.
		void init(const std::string& file_path, bool also_console = true);
		void shutdown();
		bool initialized() const { return initialized_; }

	   private:
		Logger() = default;
		~Logger();
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		std::ofstream file_;
		TeeBuf tee_cout_{};
		TeeBuf tee_cerr_{};
		std::streambuf* old_cout_{nullptr};
		std::streambuf* old_cerr_{nullptr};
		bool initialized_{false};
	};

}  // namespace rtp
