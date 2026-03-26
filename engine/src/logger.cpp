#include "log.hpp"
#include "log.h"
#include "logger.hpp"

#include "logging_easypb.h"
#include "thread_pool.hpp"

#include <easypb.hpp>
#include <chrono>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <iostream>

namespace logging {

void _detail::log(uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message) {
	// We wrap the singleton access here so the public headers don't need to know
	// anything about the Logger class or its dependencies
	Logger::log(file_name, line_number, severity, sink, message);
}

auto Logger::create() noexcept -> std::unique_ptr<Logger> {
	// Standard trick to allow make_unique with a private constructor
	struct Helper : public Logger {};
	auto ptr = std::make_unique<Helper>();
	instance = ptr.get();

	// We allow disabling this so it's easier to debug the server
	// On release this should ALWAYS be on since the client is not expected to open the log server on their own
	if constexpr (AUTO_SPAWN_LOG_SERVER) {
		try {
			std::vector<std::filesystem::path> candidates = {
#if defined(_WIN32)
				"log-server.exe",
				"build/windows/x64/debug/log-server.exe",
				"build/windows/x64/release/log-server.exe",
				"build/windows/x86_64/debug/log-server.exe",
				"build/windows/x86_64/release/log-server.exe",
#elif defined(__APPLE__)
				"log-server",
				"build/macos/arm64/debug/log-server",
				"build/macos/arm64/release/log-server",
				"build/macos/x86_64/debug/log-server",
				"build/macos/x86_64/release/log-server",
#else
				"log-server",
				"build/linux/x86_64/debug/log-server",
				"build/linux/x86_64/release/log-server",
#endif
			};

			std::filesystem::path server_path;
			for (auto &p : candidates) {
				if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) {
					server_path = p;
					break;
				}
			}

			if (!server_path.empty()) {
				std::string cmd;
				std::string output_redir = SHOW_SERVER_LOGS ? "" : " >/dev/null 2>&1";

#if defined(_WIN32)
				// On Windows, 'start /B' runs the command in the background without opening a new window
				// For output redirection, we need to wrap it in 'cmd /C'
				if (SHOW_SERVER_LOGS) {
					cmd = "start /B " + server_path.string();
				} else {
					cmd = "start /B cmd /C \"" + server_path.string() + " >nul 2>&1\"";
				}
#elif defined(__APPLE__) || defined(__linux__)
				// setsid detaches the server from the engine's process group on Linux/macOS.
				cmd = "setsid " + server_path.string() + output_redir + " &";
#endif
				if (!cmd.empty()) {
					std::system(cmd.c_str());
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

auto Logger::get() noexcept -> Logger& {
	assert(instance && "Logger doesn't exist");
	return *instance;
}

Logger::~Logger() noexcept {
	stop();
	instance = nullptr;
}

void Logger::log(std::string_view file, unsigned line, char severity, std::string_view sink, std::string_view message) {
	auto& logger = get();

	logging::LogData log;
	log.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count()); //retarded stl library
	log.set_filepath(file);
	log.set_line_number(line);
	log.set_severity(static_cast<logging::LogData_Severity>(severity));
	log.set_sink(sink);
	log.set_message(message);

	{
		std::lock_guard lock(logger.m.queue_mutex);
		logger.m.log_queue.emplace_back(std::move(log));
	}

	// We use an atomic exchange to claim a "drain slot". This ensures that
	// even if 100 threads log at once, only one background task is queued
	if (!logger.m.drain_pending.exchange(true)) {
		toast::ThreadPool::queueJob([&logger]() { logger.drain(); });
	}
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
			auto endpoints = resolver.resolve("127.0.0.1", std::to_string(PORT));
			asio::connect(m.socket, endpoints);

			// We set a send timeout so the engine doesn't hang if the log server stops responding or the TCP buffer fills up
#if defined(_WIN32)
			DWORD timeout = 1000;
			setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
			timeval timeout{.tv_sec = 1, .tv_usec = 0};
			setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
			return;
		} catch (const std::exception &e) {
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
			asio::write(m.socket, asio::buffer(batch));
		} catch (const std::exception &e) {
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

	if (batch.logs.size() == 0) return {};

	std::vector<uint8_t> buffer(batch.ByteSizeLong());
	batch.SerializeToArray(buffer.data(), buffer.size());
	return buffer;
}

void Logger::flushSync() {
	auto batch = collectQueue();
	if (!batch.empty() && m.socket.is_open()) {
		asio::error_code ec;
		asio::write(m.socket, asio::buffer(batch), ec);
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
