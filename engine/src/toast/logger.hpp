/**
 * @file logger.hpp
 * @author Xein
 * @date 16 Mar 2026
 * @brief Internal log delivery system
 *
 * Handles the heavy lifting of serializing logs and shipping them over TCP;
 * it's designed to be as "fire-and-forget" as possible for the caller
 */

#pragma once

#include "generated/logging.pb.h"

#include <asio.hpp>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace logging {

class Logger {
	inline static Logger* instance = nullptr;

	struct {
		asio::io_context io_ctx;
		asio::ip::tcp::socket socket {io_ctx};

		std::deque<logging::LogData> log_queue;
		std::mutex queue_mutex;

		/**
		 * @brief Prevents multiple background jobs from fighting over the same socket
		 *
		 * We use an atomic flag to ensure only one 'drain' job is scheduled at a time,
		 * avoiding unnecessary context switches and thread contention.
		 */
		std::atomic<bool> drain_pending = false;
	} m;

	static constexpr uint16_t port = 12800;                ///< Port to connect to the server
	static constexpr bool auto_spawn_log_server = true;    ///< Decides if the engine should create a log server or not
	static constexpr bool show_server_logs = false;        ///< If true, log server consoole will also appear on the terminal

public:
	/**
	 * @brief Set up the global logger instance, gives ownership to the caller
	 * @return A unique_ptr that manages the logger's lifetime
	 * @note This function asserts it will only be called once
	 */
	static auto create() noexcept -> std::unique_ptr<Logger>;

	/**
	 * @brief Clean shutdown: flushes remaining logs and closes the connection
	 */
	~Logger() noexcept;

	// Copying a global logger doesn't make sense
	Logger(const Logger&) = delete;
	Logger(Logger&&) = delete;
	auto operator=(const Logger&) -> Logger& = delete;
	auto operator=(Logger&&) -> Logger& = delete;

	/**
	 * @brief Entry point for all engine logs
	 *
	 * Thread-safe. It pushes the log to a local queue and signals a background
	 * task to handle the actual network I/O
	 */
	static void log(std::string_view file, unsigned line, char severity, std::string_view sink, std::string_view message);

private:
	Logger() = default;

	void initNetworkRetry();    ///< @brief Tries to establish a connection, retrying if the server isn't ready
	void stop();                ///< @brief Blocks until the background work finishes to avoid data races during shutdown
	void drain();               ///< @brief Background worker that batches queued logs and sends them
	auto collectQueue() -> std::vector<uint8_t>;    ///< @brief Pulls everything from the queue and turns it into a bit buffer
	void flushSync();    ///< @brief Synchronous fallback for when we can't rely on background threads (like shutdown)
};

}
