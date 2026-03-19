#include "logger.hpp"
#include "thread_pool.hpp"
#include <easypb.hpp>

#include <chrono>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <iostream>

auto Logger::create() noexcept -> std::unique_ptr<Logger> {
	struct Helper : public Logger {};
	auto ptr = std::make_unique<Helper>();
	instance = ptr.get();

#if defined(__linux__)
	// TODO: This needs to work in windows
	static constexpr bool AUTO_SPAWN_LOG_SERVER = false;
	if (AUTO_SPAWN_LOG_SERVER) {
		try {
			std::vector<std::filesystem::path> candidates = {
				"build/linux/x86_64/debug/log-server",
				"build/linux/x86_64/release/log-server",
			};
			std::filesystem::path server_path;
			for (auto &p : candidates) {
				if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) { server_path = p; break; }
			}
			if (!server_path.empty()) {
				std::string cmd = std::string("setsid ") + server_path.string() + " >/dev/null 2>&1 &";
				std::system(cmd.c_str());
			}
		} catch (...) {
			// best-effort spawn; ignore failures
		}
	}
#endif

	// Attempt to connect (with retries)
	ptr->init_network_retry();
	return ptr;
}

auto Logger::get() noexcept -> Logger & {
	assert(instance && "Logger doesn't exist");
	return *instance;
}

Logger::~Logger() noexcept {
	stop();
	instance = nullptr;
}

void Logger::log(std::string_view file, unsigned line_number, char severity, std::string_view sink, std::string_view message) {
	assert(instance && "Logger doesn't exist");

	logging::LogData log;
	log.set_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	log.set_filepath(file);
	log.set_line_number(line_number);
	log.set_severity(logging::LogData_Severity::TRACE);
	log.set_sink(sink);
	log.set_message(message);

	{
		std::lock_guard lock(instance->m.queue_mutex);
		instance->m.log_queue.emplace_back(std::move(log));
	}

	// Claim the drain slot. exchange(true) returns the *previous* value — if it
	// was false we just claimed it and must schedule the job; if it was true a
	// job is already in-flight and will pick up this message before it
	// re-releases the flag.
	if (!instance->m.drain_pending.exchange(true)) {
		toast::ThreadPool::queueJob([inst = instance]() { inst->drain(); });
	}
}

void Logger::init_network() {
	try {
		asio::ip::tcp::resolver resolver(m.io_ctx);
		auto endpoints = resolver.resolve(HOST, std::to_string(PORT));
		asio::connect(m.socket, endpoints);

#if defined(__linux__) || defined(__APPLE__)
		// Guard against a stalled server holding a ThreadPool worker indefinitely
		timeval timeout{.tv_sec = 1, .tv_usec = 0};
		setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout,
				sizeof(timeout));
#endif
	} catch (const std::exception &e) {
		assert(false && "Failed to connect to log server");
	}
}

void Logger::stop() {
	// Wait for any ThreadPool drain job to complete before we touch the socket.
	// Busy-spin is fine here: the write should be microseconds on loopback.
	while (m.drain_pending.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}

	// One final synchronous flush for anything logged after the last drain
	// released. No new drain jobs can be scheduled from this point — the caller
	// is responsible for ensuring log() is not called concurrently with the
	// destructor.
	flush_sync();

	if (m.socket.is_open()) {
		asio::error_code ec;
		m.socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		m.socket.close(ec);
	}
}

void Logger::drain() {
	auto batch = collect_queue();

	if (!batch.empty() && m.socket.is_open()) {
		try {
			asio::write(m.socket, asio::buffer(batch));
		} catch (const std::exception &e) {
			// TODO: reconnect logic; fall back to stderr until it's implemented
			std::fprintf(stderr, "[Logger] Send failed: %s\n", e.what());
		}
	}

	// Release the drain slot only after the write is done, then atomically check
	// if more messages arrived during the write. If so, immediately re-claim the
	// slot and re-schedule rather than letting those messages sit until the next
	// log() call.
	m.drain_pending.store(false, std::memory_order_release);
	{
		std::lock_guard lock(m.queue_mutex);
		if (!m.log_queue.empty() && !m.drain_pending.exchange(true)) {
			toast::ThreadPool::queueJob([this]() { drain(); });
		}
	}
}

auto Logger::collect_queue() -> std::vector<uint8_t> {
	// create a batch with all the logs to dispatch
	logging::LogBatch batch;

	{
		std::lock_guard lock(m.queue_mutex);
		while (!m.log_queue.empty()) {
			auto* new_log = batch.add_logs();
			*new_log = std::move(m.log_queue.front());
			m.log_queue.pop_front();
		}
	}

	// serialize the batch into binary and send it
	std::vector<uint8_t> buffer(batch.ByteSizeLong());
	batch.SerializeToArray(buffer.data(), buffer.size());
	return buffer;
}

void Logger::flush_sync() {
	auto batch = collect_queue();
	if (!batch.empty() && m.socket.is_open()) {
		asio::error_code ec;
		asio::write(m.socket, asio::buffer(batch), ec);
		if (ec) {
			std::fprintf(stderr, "[Logger] Final flush failed: %s\n",
					ec.message().c_str());
		}
	}
}

void Logger::init_network_retry() {
	// Try connecting with retries instead of aborting — make logging non-fatal
	const int max_attempts = 50; // ~10s at base_delay_ms=200
	const int base_delay_ms = 200;
	for (int attempt = 1; attempt <= max_attempts; ++attempt) {
		try {
			// Ensure socket is fresh
			if (m.socket.is_open()) {
				asio::error_code ec;
				m.socket.close(ec);
			}
			m.socket = asio::ip::tcp::socket(m.io_ctx);

			asio::ip::tcp::resolver resolver(m.io_ctx);
			auto endpoints = resolver.resolve(HOST, std::to_string(PORT));
			asio::connect(m.socket, endpoints);

		#if defined(__linux__) || defined(__APPLE__)
			// Guard against a stalled server holding a ThreadPool worker indefinitely
			timeval timeout{.tv_sec = 1, .tv_usec = 0};
			setsockopt(m.socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &timeout,
					sizeof(timeout));
		#endif

			std::fprintf(stderr, "[Logger] Connected to log server (attempt %d)\n", attempt);
			return;
		} catch (const std::exception &e) {
			if (attempt == max_attempts) {
				std::fprintf(stderr, "[Logger] Failed to connect to log server after %d attempts: %s\n", max_attempts, e.what());
			} else {
				std::fprintf(stderr, "[Logger] Connect attempt %d failed: %s; retrying in %dms\n", attempt, e.what(), base_delay_ms * attempt);
				std::this_thread::sleep_for(std::chrono::milliseconds(base_delay_ms * attempt));
			}
		}
	}
}

