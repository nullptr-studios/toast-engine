#include "logger.hpp"

#include "ffi/log.h"    // ffi
#include "generated/logging_easypb.h"
#include "log.hpp"      // public functions
#include "thread_pool.hpp"

#include <chrono>
#include <cstdlib>
#include <easypb.hpp>
#include <filesystem>
#include <iostream>
#include <print>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace logging {

void _detail::log(
    uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message
) {
	// We wrap the singleton access here so the public headers don't need to know
	// anything about the Logger class or its dependencies
	Logger::log(file_name, line_number, severity, sink, message);
}

auto Logger::create() noexcept -> std::unique_ptr<Logger> {
	// Standard trick to allow make_unique with a private constructor
	struct Helper : public Logger { };

	auto ptr = std::make_unique<Helper>();
	instance = ptr.get();

	// We allow disabling this so it's easier to debug the server
	// On release this should ALWAYS be on since the client is not expected to open the log server on their own
	if constexpr (auto_spawn_log_server) {
		try {
			// Get the directory of the current executable
			std::filesystem::path exe_dir;
			try {
				// Try to get the executable path using platform-specific methods
#ifdef __linux__
				std::array<char, 4096> exe_path;
				ssize_t len = readlink("/proc/self/exe", exe_path.data(), exe_path.size() - 1);
				if (len != -1) {
					exe_path[len] = '\0';
					exe_dir = std::filesystem::path(exe_path.data()).parent_path();
				}
#elif defined(__APPLE__)
				uint32_t size = 4096;
				char exe_path[size];
				if (_NSGetExecutablePath(exe_path, &size) == 0) {
					exe_dir = std::filesystem::path(exe_path).parent_path();
				}
#elif defined(_WIN32)
				char exe_path[MAX_PATH];
				if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) {
					exe_dir = std::filesystem::path(exe_path).parent_path();
				}
#endif
			} catch (...) { std::println(std::cerr, "Couldnt find log server executable"); }

			std::vector<std::filesystem::path> candidates;

			if (!exe_dir.empty()) {
#if defined(_WIN32)
				candidates.push_back(exe_dir / "log_server.exe");
#else
				candidates.push_back(exe_dir / "log_server");
#endif
			}

			std::filesystem::path server_path;
			for (auto& p : candidates) {
				if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) {
					server_path = p;
					break;
				}
			}

			if (!server_path.empty()) {
				std::string cmd;
				std::string output_redir = show_server_logs ? "" : " >/dev/null 2>&1";

#if defined(_WIN32)
				// On Windows, 'start /B' runs the command in the background without opening a new window
				// For output redirection, we need to wrap it in 'cmd /C'
				if (show_server_logs) {
					cmd = "start /B " + server_path.string();
				} else {
					cmd = "start /B cmd /C \"" + server_path.string() + " >nul 2>&1\"";
				}
#elif defined(__APPLE__) || defined(__linux__)
				// setsid detaches the server from the engine's process group on Linux/macOS.
				// The trailing & ensures std::system() returns immediately without waiting
				cmd = "setsid " + server_path.string() + output_redir + " &";
#endif
				if (!cmd.empty()) {
					int ret = std::system(cmd.c_str());
					if (ret != 0) {
						std::println(std::cerr, "[Logger] Failed to execute log server spawn command: {}", ret);
					}
				}
			} else {
				std::println(std::cerr, "[Logger] Could not find log server binary. Candidates checked:");
				for (const auto& candidate : candidates) {
					std::println(std::cerr, "  - {}", candidate.string());
				}
			}
		} catch (...) {
			std::println(std::cerr, "[Logger] Failed to spawn log server");
			abort();
		}
	}

	ptr->initNetworkRetry();
	return ptr;
}

Logger::~Logger() noexcept {
	stop();
	instance = nullptr;
}

void Logger::log(std::string_view file, unsigned line, char severity, std::string_view sink, std::string_view message) {
	auto* logger = instance;

	if (not logger) {
		switch (severity) {
			case 4:     // critical
			case 3:     // error
				std::println("\033[31m[ERROR] {}: {}\033[0m", sink, message);
				return;
			case 2:     // warning
				std::println("\033[33m[WARNING] {}: {}\033[0m", sink, message);
				return;
			case 1:     // info
				std::println("\033[32m[INFO] {}: {}\033[0m", sink, message);
				return;
			default:    // trace
				std::println("[TRACE] {}: {}", sink, message);
				return;
		}
	}

	logging::LogData log;
	log.set_timestamp(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
	);    // retarded stl library
	log.set_filepath(file);
	log.set_line_number(line);
	log.set_severity(static_cast<logging::LogData_Severity>(severity));
	log.set_sink(sink);
	log.set_message(message);

	{
		std::lock_guard lock(logger->m.queue_mutex);
		logger->m.log_queue.emplace_back(std::move(log));
	}

	// We use an atomic exchange to claim a "drain slot". This ensures that
	// even if 100 threads log at once, only one background task is queued
	if (!logger->m.drain_pending.exchange(true)) {
		toast::ThreadPool::queueJob([&logger]() { logger->drain(); });
	}

#ifdef DEBUG
	// If in debug we are going to print warnings and errors on console
	if (severity >= 3) {
		std::println("\033[31m[ERROR] {}: {}\033[0m", sink, message);
	} else if (severity == 2) {
		std::println("\033[33m[WARNING] {}: {}\033[0m", sink, message);
	}
#endif
}

void Logger::initNetworkRetry() {
	// The server might be slow to start, so we give it a few seconds
	// to avoid crashing the engine immediately on boot
	constexpr int max_attempts = 10;
	constexpr int delay_ms = 1000;

	for (int attempt = 1; attempt <= max_attempts; ++attempt) {
		try {
			if (m.socket.is_open()) {
				asio::error_code ec;
				[[maybe_unused]]
				auto close_result = m.socket.close(ec);
			}
			m.socket = asio::ip::tcp::socket(m.io_ctx);

			asio::ip::tcp::resolver resolver(m.io_ctx);
			auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
			asio::connect(m.socket, endpoints);

			// We set a send timeout so the engine doesn't hang if the log server stops responding or the TCP buffer fills up
#if defined(_WIN32)
			DWORD timeout = 1000;
			setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
			timeval timeout {.tv_sec = 1, .tv_usec = 0};
			setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
			return;
		} catch (const std::exception& e) {
			if (attempt == max_attempts) {
				std::println(std::cerr, "[Logger] Failed to connect after {} attempts: {}", max_attempts, e.what());
				abort();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
		}
	}
}

void Logger::stop() {
	// We spin briefly to wait for any active background write to finish
	// This prevents us from closing the socket while a ThreadPool worker is using it
	while (m.drain_pending.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}

	// Any logs produced during the last drain or while we were waiting above
	// must be flushed synchronously before the object is destroyed
	flushSync();

	if (m.socket.is_open()) {
		asio::error_code ec;
		[[maybe_unused]]
		auto shutdown_result = m.socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		[[maybe_unused]]
		auto close_result = m.socket.close(ec);
	}
}

void Logger::drain() {
	auto batch = collectQueue();

	if (!batch.empty() && m.socket.is_open()) {
		try {
			uint32_t len = static_cast<uint32_t>(batch.size());
			std::array<uint8_t, 4> len_buf;
			len_buf[0] = (len >> 24) & 0xFF;
			len_buf[1] = (len >> 16) & 0xFF;
			len_buf[2] = (len >> 8) & 0xFF;
			len_buf[3] = len & 0xFF;

			std::array<asio::const_buffer, 2> bufs = {asio::buffer(len_buf, 4), asio::buffer(batch)};
			asio::write(m.socket, bufs);
		} catch (const std::exception& e) {
			// If sending fails, we fallback to stderr to avoid losing critical info
			std::println(std::cerr, "[Logger] Send failure: {}", e.what());
		}
	}

	// Release the drain slot, but check if more logs arrived while we were busy
	// This "double-check" pattern ensures the queue is actually empty when we finish
	m.drain_pending.store(false, std::memory_order_release);
	{
		std::lock_guard lock(m.queue_mutex);
		if (!m.log_queue.empty() && !m.drain_pending.exchange(true)) {
			toast::ThreadPool::queueJob([this]() { drain(); });
		}
	}
}

auto Logger::collectQueue() -> std::vector<uint8_t> {
	// Batching logs together significantly reduces the number of TCP packets
	// and system calls, which is better for performance
	logging::LogBatch batch;
	{
		std::lock_guard lock(m.queue_mutex);
		while (!m.log_queue.empty()) {
			auto* new_log = batch.add_logs();
			*new_log = std::move(m.log_queue.front());
			m.log_queue.pop_front();
		}
	}

	if (batch.logs.empty()) {
		return {};
	}

	std::vector<uint8_t> buffer(batch.ByteSizeLong());
	batch.SerializeToArray(buffer.data(), buffer.size());
	return buffer;
}

void Logger::flushSync() {
	auto batch = collectQueue();
	if (!batch.empty() && m.socket.is_open()) {
		uint32_t len = static_cast<uint32_t>(batch.size());
		std::array<uint8_t, 4> len_buf;
		len_buf[0] = (len >> 24) & 0xFF;
		len_buf[1] = (len >> 16) & 0xFF;
		len_buf[2] = (len >> 8) & 0xFF;
		len_buf[3] = len & 0xFF;

		std::array<asio::const_buffer, 2> bufs = {asio::buffer(len_buf, 4), asio::buffer(batch)};
		asio::error_code ec;
		asio::write(m.socket, bufs, ec);
	}
}

}

extern "C" {
using namespace logging;

void toast_trace(const char* sink, const char* message, const char* file, unsigned line) {
	Logger::log(file, line, 0, sink, message);
}

void toast_info(const char* sink, const char* message, const char* file, unsigned line) {
	Logger::log(file, line, 1, sink, message);
}

void toast_warn(const char* sink, const char* message, const char* file, unsigned line) {
	Logger::log(file, line, 2, sink, message);
}

void toast_error(const char* sink, const char* message, const char* file, unsigned line) {
	Logger::log(file, line, 3, sink, message);
}

void toast_critical(const char* sink, const char* message, const char* file, unsigned line) {
	Logger::log(file, line, 4, sink, message);
}
}
