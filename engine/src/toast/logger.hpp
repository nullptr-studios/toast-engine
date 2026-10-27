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

#include "backtrace.hpp"
#include "generated/logging.pb.h"

#include <asio.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace logging {

/// log plus its raw trace
struct Pending {
	proto::logging::LogData data;
	RawBacktrace trace;
};

class Logger {
	inline static Logger* instance = nullptr;

	struct {
		asio::io_context io_ctx;
		asio::ip::tcp::socket socket {io_ctx};

		// send_mutex before queue_mutex

		std::deque<Pending> log_queue;
		std::mutex queue_mutex;
		/// records ever queued
		uint64_t enqueued = 0;

		/// held while writing to the socket
		std::timed_mutex send_mutex;
		std::atomic<uint64_t> sent = 0;       ///< records that left the queue
		std::atomic<bool> stopped = false;    ///< set by stop

		/**
		 * @brief Means a drain job is queued or running
		 */
		std::atomic<bool> drain_pending = false;

		/**
		 * @brief True once initNetworkRetry has established the connection
		 */
		std::atomic<bool> connected = false;

		/**
		 * @brief Set once initNetworkRetry has given up
		 *
		 */
		std::atomic<bool> unavailable = false;
	} m;

	static constexpr uint16_t port = 12800;                ///< Port to connect to the server
	static constexpr bool auto_spawn_log_server = true;    ///< Decides if the engine should create a log server or not
	static constexpr bool show_server_logs = false;        ///< If true, log server consoole will also appear on the terminal
	static constexpr std::size_t k_max_frame_bytes = 4 * 1024 * 1024;    ///< largest batch per frame

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
	static void
	    log(std::string_view file, unsigned line, char severity, std::string_view sink, std::string_view message,
			    std::string_view details = {});

	/// sends what is queued on the calling thread
	static void flush() noexcept;

	/// logs a crash and sends everything queued
	static void crashFlush(std::string_view reason, const RawBacktrace& trace) noexcept;

private:
	Logger() = default;

	void initNetworkRetry();    ///< @brief Tries to establish a connection, retrying if the server isn't ready
	void stop();                ///< @brief Blocks until the background work finishes to avoid data races during shutdown
	void drain();               ///< @brief Background worker that batches queued logs and sends them
	/// pulls queued records into a batch
	auto collectQueue(std::size_t max_bytes) -> std::vector<uint8_t>;
	/// never sends an empty payload
	auto writeFrame(const std::vector<uint8_t>& payload) -> asio::error_code;
	/**
	 * @brief Synchronous fallback for when we can't rely on background threads (like shutdown)
	 */
	void flushSync(
	    uint64_t target = std::numeric_limits<uint64_t>::max(),
	    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()
	);
};

}
