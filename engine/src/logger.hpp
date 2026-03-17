/**
 * @file logger.hpp
 * @author Xein
 * @date 16/03/26
 * @brief Handles logging on the project: serializes logs and sends them through TCP/IP
 *        to the logger server
 *
 * @note NOTE: Protobuf serialization is not yet implemented — messages are currently sent as
 *       raw newline-terminated strings.
 */

#pragma once

#include <asio.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <deque>
#include <mutex>
#include <atomic>
#include <cassert>
#include <cstdio>
#include "logging_easypb.h"
#include "toast/thread_pool.hpp"

class Logger {
	inline static Logger* instance = nullptr;

	struct {
		asio::io_context io_ctx;
		asio::ip::tcp::socket socket{ io_ctx }; // needs to be after io_ctx

		std::deque<logging::LogData> log_queue;
		std::mutex queue_mutex;

		/// Guards the socket against concurrent drain() calls
		/// true  -> a drain job is in-flight; callers must not schedule another
		/// false -> no drain is running; the next log() call will schedule one
		std::atomic<bool> drain_pending = false;
	} m;

public:
	static constexpr std::string_view HOST = "127.0.0.1"; ///< localhost, DO NOT CHANGE
	static constexpr uint16_t PORT = 12800;               ///< Port used to load the server

	/**
	 * @brief Creates the singleton Logger instance and opens the TCP connection.
	 *
	 * Must be called before any call to log() or get(). The returned pointer owns the
	 * logger and destroying it shuts the logger down cleanly
	 */
	static auto create() noexcept -> std::unique_ptr<Logger>;

	/**
	 * @brief Retrieves the active Logger instance.
	 * @note Asserts in debug if called before create()
	 */
	static auto get() noexcept -> Logger &;

	/**
	 * @brief Flushes any remaining messages and closes the connection.
	 */
	~Logger() noexcept;

	// Remove copy and move semantics
	Logger(const Logger&) = delete;
	Logger(Logger&&) = delete;
	Logger& operator=(const Logger&) = delete;
	Logger& operator=(Logger&&) = delete;

	/**
	 * @brief Enqueues a message for delivery to the log server.
	 *
	 * Thread-safe. Returns immediately as the actual TCP send happens on a ThreadPool
	 * worker. If a drain job is already pending, this message will be picked up by it
	 * or the one after it; no drain job is double-scheduled
	 *
	 * @param message The log string to send. A newline is appended automatically.
	 *
	 * HACK: This is not the actual implementation of log()
	 */
	static void log(std::string_view file, unsigned line_number, char severity, std::string_view sink, std::string_view message);

private:
	Logger() = default;

	/**
	 * @brief Synchronously connects the socket to the log server
	 *
	 * TODO: Should hold the engine until connected to logger
	 */
	void init_network();

	/**
	 * @brief Waits for any in-flight drain job to finish, then sends remaining messages
	 *        and closes the socket.
	 *
	 * The spin on drain_pending is brief — it only lasts as long as the current write.
	 */
	void stop();

	/**
	 * @brief Drains the queue and sends its contents over TCP.
	 *
	 * Executed on a ThreadPool worker thread. At most one drain() runs at a time —
	 * drain_pending guarantees this. The flag is only released *after* the write
	 * completes; the queue is then re-checked atomically so that any messages
	 * logged during the write are not silently dropped.
	 */
	void drain();

	/**
	 * @brief Collects all pending messages from the queue into a single string.
	 * @return Concatenated batch, or an empty string if the queue was empty.
	 */
	auto collect_queue() -> std::vector<uint8_t>;

	/**
	 * @brief Sends any remaining queued messages synchronously. Called from stop().
	 *
	 * Assumes drain_pending is false and no concurrent drain() is running.
	 */
	void flush_sync();

};
