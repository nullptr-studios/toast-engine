#include "logger.hpp"

#include "ffi/log.h"    // ffi
#include "generated/logging.pb.h"
#include "log.hpp"      // public functions
#include "thread_pool.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <google/protobuf/io/coded_stream.h>
#include <iostream>
#include <iterator>
#include <print>
#include <string>
#include <thread>
#include <tracy/Tracy.hpp>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace logging {

namespace {
/// Directory of the binary that owns this code
auto moduleDirectory() -> std::filesystem::path {
	try {
#if defined(_WIN32)
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(
		        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		        reinterpret_cast<LPCSTR>(&moduleDirectory),
		        &module
		    )) {
			return {};
		}
		std::array<char, MAX_PATH> path;
		if (GetModuleFileNameA(module, path.data(), MAX_PATH) == 0) {
			return {};
		}
		return std::filesystem::path(path.data()).parent_path();
#elif defined(__linux__) || defined(__APPLE__)
		Dl_info info;
		if (dladdr(reinterpret_cast<const void*>(&moduleDirectory), &info) != 0 && info.dli_fname != nullptr) {
			return std::filesystem::path(info.dli_fname).parent_path();
		}
		return {};
#else
		return {};
#endif
	} catch (...) { return {}; }
}

std::mutex fallback_database_mutex;
std::vector<Pending> fallback_database;    ///< logged before the logger existed

/// true while this thread holds send_mutex
thread_local bool t_sending = false;

struct SendingScope {
	SendingScope() noexcept { t_sending = true; }

	~SendingScope() noexcept { t_sending = false; }

	SendingScope(const SendingScope&) = delete;
	auto operator=(const SendingScope&) -> SendingScope& = delete;
};

/// longest text kept in one field
constexpr std::size_t k_max_field_bytes = 1024 * 1024;
constexpr std::string_view k_truncated_suffix = "\xE2\x80\xA6[truncated]";    // ellipsis then truncated
constexpr std::string_view k_replacement_character = "\xEF\xBF\xBD";          // U+FFFD

struct Utf8Sequence {
	std::size_t length = 0;                                                     ///< bytes it spans
	bool valid = false;
};

/// reads one utf8 sequence per the unicode table
auto utf8SequenceAt(std::string_view text) -> Utf8Sequence {
	const auto byte = [text](std::size_t i) -> unsigned { return static_cast<unsigned char>(text[i]); };
	const unsigned lead = byte(0);
	if (lead < 0x80) {
		return {.length = 1, .valid = true};
	}

	std::size_t length = 0;
	unsigned second_min = 0x80;
	unsigned second_max = 0xBF;
	if (lead >= 0xC2 && lead <= 0xDF) {
		length = 2;
	} else if (lead >= 0xE0 && lead <= 0xEF) {
		length = 3;
		second_min = lead == 0xE0 ? 0xA0 : 0x80;
		second_max = lead == 0xED ? 0x9F : 0xBF;
	} else if (lead >= 0xF0 && lead <= 0xF4) {
		length = 4;
		second_min = lead == 0xF0 ? 0x90 : 0x80;
		second_max = lead == 0xF4 ? 0x8F : 0xBF;
	} else {
		return {.length = 1, .valid = false};
	}

	for (std::size_t i = 1; i < length; ++i) {
		const unsigned min = i == 1 ? second_min : 0x80;
		const unsigned max = i == 1 ? second_max : 0xBF;
		if (i >= text.size() || byte(i) < min || byte(i) > max) {
			return {.length = i, .valid = false};
		}
	}
	return {.length = length, .valid = true};
}

/// replaces invalid utf8 and cuts long text
auto sanitizeField(std::string_view text) -> std::string {
	std::string result;
	std::size_t copied = 0;    // text before this offset is already in result
	std::size_t offset = 0;
	// stops once past the limit
	while (offset < text.size() && result.size() + (offset - copied) <= k_max_field_bytes) {
		const Utf8Sequence sequence = utf8SequenceAt(text.substr(offset));
		if (!sequence.valid) {
			result.append(text.substr(copied, offset - copied));
			result.append(k_replacement_character);
			copied = offset + sequence.length;
		}
		offset += sequence.length;
	}
	result.append(text.substr(copied, offset - copied));

	if (result.size() > k_max_field_bytes) {
		// back to the start of a character
		std::size_t cut = k_max_field_bytes - k_truncated_suffix.size();
		while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) {
			--cut;
		}
		result.resize(cut);
		result.append(k_truncated_suffix);
	}
	return result;
}
}

void _detail::log(
    uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message
) {
	_detail::log(severity, file_name, line_number, sink, message, {});
}

void _detail::log(
    uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message,
    std::string_view details
) {
	// We wrap the singleton access here so the public headers don't need to know
	// anything about the Logger class or its dependencies
	Logger::log(file_name, line_number, severity, sink, message, details);
}

void flush() noexcept {
	Logger::flush();
}

auto Logger::create() noexcept -> std::unique_ptr<Logger> {
	ZoneScoped;

	// Standard trick to allow make_unique with a private constructor
	struct Helper : public Logger { };

	auto ptr = std::make_unique<Helper>();
	instance = ptr.get();

	toast::ThreadPool::push([] {
		// load the symbol tables so the first error does not wait
		warmUpBacktrace();

		// older than anything queued since so they go in front
		std::vector<Pending> early_logs;
		{
			std::lock_guard lock(fallback_database_mutex);
			early_logs.swap(fallback_database);
		}
		{
			std::lock_guard lock(instance->m.queue_mutex);
			instance->m.log_queue.insert(
			    instance->m.log_queue.begin(), std::make_move_iterator(early_logs.begin()), std::make_move_iterator(early_logs.end())
			);
			instance->m.enqueued += early_logs.size();
		}

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
					std::array<char, MAX_PATH> exe_path;
					if (GetModuleFileNameA(nullptr, exe_path.data(), MAX_PATH)) {
						exe_dir = std::filesystem::path(exe_path.data()).parent_path();
					}
#endif
				} catch (...) { std::println(std::cerr, "Couldnt find log server executable"); }

#if defined(_WIN32)
				constexpr std::string_view server_name = "log_server.exe";
#else
				constexpr std::string_view server_name = "log_server";
#endif

				// Module first
				std::vector<std::filesystem::path> candidates;
				if (auto module_dir = moduleDirectory(); !module_dir.empty()) {
					candidates.push_back(module_dir / server_name);
				}
				if (!exe_dir.empty()) {
					candidates.push_back(exe_dir / server_name);
				}

				std::filesystem::path server_path;
				for (auto& p : candidates) {
					if (std::filesystem::exists(p) && std::filesystem::is_regular_file(p)) {
						server_path = p;
						break;
					}
				}

				if (!server_path.empty()) {
#if defined(_WIN32)
					STARTUPINFOA si;
					PROCESS_INFORMATION pi;
					ZeroMemory(&si, sizeof(si));
					si.cb = sizeof(si);
					ZeroMemory(&pi, sizeof(pi));

					std::string cmd_args = server_path.string();
					DWORD creation_flags = CREATE_NO_WINDOW;

					if (CreateProcessA(nullptr, cmd_args.data(), nullptr, nullptr, FALSE, creation_flags, nullptr, nullptr, &si, &pi)) {
						// Close handles immediately so the log server runs independently
						CloseHandle(pi.hProcess);
						CloseHandle(pi.hThread);
					} else {
						std::println(std::cerr, "[Logger] CreateProcessA failed with error: {}", GetLastError());
					}
#elif defined(__APPLE__) || defined(__linux__)
					std::string cmd;
					std::string output_redir = show_server_logs ? "" : " >/dev/null 2>&1";

					cmd = "setsid " + server_path.string() + output_redir + " &";

					if (!cmd.empty()) {
						int ret = std::system(cmd.c_str());
						if (ret != 0) {
							std::println(std::cerr, "[Logger] Failed to execute log server spawn command: {}", ret);
						}
					}
#endif
				} else {
					std::println(std::cerr, "[Logger] Could not find log server binary. Candidates checked:");
					for (const auto& candidate : candidates) {
						std::println(std::cerr, "  - {}", candidate.string());
					}
				}
			} catch (...) { std::println(std::cerr, "[Logger] Failed to spawn log server"); }
		}

		instance->initNetworkRetry();
	});
	return ptr;
}

Logger::~Logger() noexcept {
	// first so callers stop reaching a dying logger
	instance = nullptr;
	stop();
}

void Logger::log(
    std::string_view file, unsigned line, char severity, std::string_view sink, std::string_view message, std::string_view details
) {
	// first while the stack is still the callers
	const RawBacktrace trace = captureBacktrace();
	auto* logger = instance;

	// DEPRECATED: This is not used anymore
	auto pos = sink.find_last_of(':');
	std::string_view trimmed_sink;
	if (pos != std::string::npos) {
		trimmed_sink = sink.substr(pos + 1);
	} else {
		trimmed_sink = sink;
	}

	// nobody to receive
	if (logger != nullptr && logger->m.unavailable.load(std::memory_order_acquire)) {
		return;
	}

	// collectQueue fills the backtrace
	proto::logging::LogData log;
	log.set_timestamp(
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
	);    // retarded stl library
	log.set_filepath(sanitizeField(file));
	log.set_line_number(line);
	log.set_severity(static_cast<proto::logging::LogData_Severity>(severity));
	log.set_sink(sanitizeField(trimmed_sink));
	log.set_message(sanitizeField(message));
	log.set_details(sanitizeField(details));

	if (not logger) {
#ifdef DEBUG
		try {
			switch (severity) {
				case 4:     // critical
				case 3:     // error
					std::println("\033[31m[ERROR] {}: {}\033[0m", trimmed_sink, message);
					break;
				case 2:     // warning
					std::println("\033[33m[WARNING] {}: {}\033[0m", trimmed_sink, message);
					break;
				case 1:     // info
					std::println("\033[32m[INFO] {}: {}\033[0m", trimmed_sink, message);
					break;
				default:    // trace
					std::println("[TRACE] {}: {}", trimmed_sink, message);
					break;
			}
		} catch (...) {
			// std::println can sometimes throw if cstdout doesnt exist yet so rather than printing it just add it to the fallback
			// database (down below) and do nothing else
			// Trully c# moment this only happens sometimes because of some bullshit of the .NET worker execution pool -x
			(void)0;    // avoid clang-tidy error about empty catch block
		}

#endif

		// Add the logs to a fallback database if it doesn't exist, then send them when initialized
		std::lock_guard lock(fallback_database_mutex);
		fallback_database.push_back({.data = std::move(log), .trace = trace});
		return;
	}

	{
		std::lock_guard lock(logger->m.queue_mutex);
		logger->m.log_queue.push_back({.data = std::move(log), .trace = trace});
		++logger->m.enqueued;
	}

	// We use an atomic exchange to claim a "drain slot". This ensures that
	// even if 100 threads log at once, only one background task is queued
	if (!logger->m.drain_pending.exchange(true)) {
		toast::ThreadPool::push([logger]() { logger->drain(); });
	}

	// errors leave before the caller continues
	if (severity >= 3) {
		flush();
	}

#ifdef DEBUG
	// If in debug we are going to print warnings and errors on console
	// if (severity >= 3) {
	// 	// FIXME: I GET A CRASH HERE WITH VK VALIDATION LAYERS!?!?!
	// 	//  std::println("\033[31m[ERROR] {}: {}\033[0m", trimmed_sink, message);
	// } else if (severity == 2) {
	// 	// std::println("\033[33m[WARNING] {}: {}\033[0m", trimmed_sink, message);
	// yeah this crashes with validation layers idk why
	// }
#endif
}

void Logger::initNetworkRetry() {
	ZoneScoped;

	// The server might be slow to start, so we give it a few seconds
	// to avoid crashing the engine immediately on boot
	constexpr int max_attempts = 10;
	constexpr int delay_ms = 1000;

	for (int attempt = 1; attempt <= max_attempts; ++attempt) {
		ZoneScopedN("Connection attempt");
		ZoneNameF("Connection attempt %i", attempt);

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
			// Publish the connection, then flush anything that queued up while we were still connecting
			m.connected.store(true, std::memory_order_release);
			if (!m.drain_pending.exchange(true)) {
				toast::ThreadPool::push([this] { drain(); });
			}
			return;
		} catch (const std::exception& e) {
			if (attempt == max_attempts) {
				std::println(std::cerr, "[Logger] Failed to connect after {} attempts: {}", max_attempts, e.what());
				std::println(std::cerr, "[Logger] Continuing without log delivery, records will be discarded");
				m.unavailable.store(true, std::memory_order_release);

				// Whatever queued up while we were retrying has nowhere to go
				if (!m.drain_pending.exchange(true)) {
					toast::ThreadPool::push([this] { drain(); });
				}
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
		}
	}
}

void Logger::stop() {
	ZoneScoped;

	// We spin briefly to wait for any active background drain to finish
	// claim the slot so no drain runs after this
	while (m.drain_pending.exchange(true, std::memory_order_acq_rel)) {
		std::this_thread::yield();
	}
	// a drain is gone once this lock was held
	m.queue_mutex.lock();
	m.queue_mutex.unlock();

	// wait for a flush on another thread
	std::lock_guard send_lock(m.send_mutex);
	const SendingScope sending;
	m.stopped.store(true, std::memory_order_release);

	// Any logs produced during the last drain or while we were waiting above
	// must be flushed synchronously before the object is destroyed
	flushSync();

	// zero length frame is the goodbye
	if (m.connected.load(std::memory_order_acquire) && m.socket.is_open()) {
		constexpr std::array<uint8_t, 4> goodbye {};
		asio::error_code ec;
		asio::write(m.socket, asio::buffer(goodbye), ec);
	}

	m.connected.store(false, std::memory_order_release);

	if (m.socket.is_open()) {
		asio::error_code ec;
		[[maybe_unused]]
		auto shutdown_result = m.socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		[[maybe_unused]]
		auto close_result = m.socket.close(ec);
	}
}

void Logger::drain() {
	ZoneScoped;

	// every exit gives the slot back under queue_mutex
	if (m.unavailable.load(std::memory_order_acquire)) {
		// destroyed outside the lock
		std::deque<Pending> discarded;
		std::lock_guard lock(m.queue_mutex);
		discarded.swap(m.log_queue);
		m.sent.fetch_add(discarded.size(), std::memory_order_relaxed);
		m.drain_pending.store(false, std::memory_order_release);
		return;
	}

	// The socket is open but not yet connected while initNetworkRetry is still running
	if (!m.connected.load(std::memory_order_acquire)) {
		std::lock_guard lock(m.queue_mutex);
		m.drain_pending.store(false, std::memory_order_release);
		return;
	}

	// bursts go out as several frames
	// send_mutex is taken per frame
	try {
		while (true) {
			std::lock_guard send_lock(m.send_mutex);
			const SendingScope sending;
			const std::vector<uint8_t> batch = collectQueue(k_max_frame_bytes);
			if (batch.empty()) {
				break;
			}
			if (const asio::error_code ec = writeFrame(batch)) {
				// If sending fails, we fallback to stderr to avoid losing critical info
				std::println(std::cerr, "[Logger] Send failure: {}", ec.message());
			}
		}
	} catch (const std::exception& e) { std::println(std::cerr, "[Logger] Send failure: {}", e.what()); }

	// Release the drain slot, but check if more logs arrived while we were busy
	// This "double-check" pattern ensures the queue is actually empty when we finish
	std::lock_guard lock(m.queue_mutex);
	m.drain_pending.store(false, std::memory_order_release);
	if (!m.log_queue.empty() && !m.drain_pending.exchange(true)) {
		toast::ThreadPool::push([this]() { drain(); });
	}
}

auto Logger::collectQueue(std::size_t max_bytes) -> std::vector<uint8_t> {
	ZoneScoped;

	// records only grow once their trace is formatted
	std::vector<Pending> taken;
	{
		std::lock_guard lock(m.queue_mutex);
		std::size_t untraced_bytes = 0;
		while (!m.log_queue.empty() && (taken.empty() || untraced_bytes < max_bytes)) {
			untraced_bytes += m.log_queue.front().data.ByteSizeLong();
			taken.push_back(std::move(m.log_queue.front()));
			m.log_queue.pop_front();
		}
	}

	if (taken.empty()) {
		return {};
	}

	// Batching logs together significantly reduces the number of TCP packets
	// and system calls, which is better for performance
	// symbols resolve outside the lock
	proto::logging::LogBatch batch;
	std::size_t batch_bytes = 0;
	std::size_t used = 0;
	for (; used < taken.size(); ++used) {
		proto::logging::LogData& data = taken[used].data;
		data.set_backtrace(sanitizeField(formatBacktrace(taken[used].trace)));

		// tag length and record
		const std::size_t data_bytes = data.ByteSizeLong();
		const std::size_t entry_bytes = 1 + google::protobuf::io::CodedOutputStream::VarintSize64(data_bytes) + data_bytes;
		if (used > 0 && batch_bytes + entry_bytes > max_bytes) {
			break;
		}
		batch_bytes += entry_bytes;
		*batch.add_logs() = std::move(data);
	}
	// a failed write loses them too
	m.sent.fetch_add(used, std::memory_order_relaxed);

	if (used < taken.size()) {
		// what did not fit goes back in front
		std::lock_guard lock(m.queue_mutex);
		m.log_queue.insert(
		    m.log_queue.begin(),
		    std::make_move_iterator(taken.begin() + static_cast<std::ptrdiff_t>(used)),
		    std::make_move_iterator(taken.end())
		);
	}

	std::vector<uint8_t> buffer(batch.ByteSizeLong());
	batch.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
	return buffer;
}

auto Logger::writeFrame(const std::vector<uint8_t>& payload) -> asio::error_code {
	assert(!payload.empty() && "a frame of length 0 is the goodbye");

	uint32_t len = static_cast<uint32_t>(payload.size());
	std::array<uint8_t, 4> len_buf;
	len_buf[0] = (len >> 24) & 0xFF;
	len_buf[1] = (len >> 16) & 0xFF;
	len_buf[2] = (len >> 8) & 0xFF;
	len_buf[3] = len & 0xFF;

	std::array<asio::const_buffer, 2> bufs = {asio::buffer(len_buf, 4), asio::buffer(payload)};
	asio::error_code ec;
	asio::write(m.socket, bufs, ec);
	return ec;
}

void Logger::flush() noexcept {
	auto* logger = instance;
	// t_sending avoids locking send_mutex twice
	if (logger == nullptr || t_sending || logger->m.stopped.load(std::memory_order_acquire)) {
		return;
	}

	try {
		// only what is queued now
		uint64_t target = 0;
		{
			std::lock_guard lock(logger->m.queue_mutex);
			target = logger->m.enqueued;
		}

		// the socket has its own lock so a drain job cannot hold this up
		constexpr auto k_timeout = std::chrono::milliseconds(500);
		const auto deadline = std::chrono::steady_clock::now() + k_timeout;
		std::unique_lock<std::timed_mutex> send_lock(logger->m.send_mutex, std::defer_lock);
		if (!send_lock.try_lock_until(deadline)) {
			return;
		}
		const SendingScope sending;
		if (!logger->m.stopped.load(std::memory_order_acquire)) {
			logger->flushSync(target, deadline);
		}
	} catch (...) { (void)0; }
}

void Logger::crashFlush(std::string_view reason, const RawBacktrace& trace) noexcept {
	auto* logger = instance;
	if (logger == nullptr) {
		return;
	}

	try {
		proto::logging::LogData data;
		data.set_timestamp(
		    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
		);
		data.set_severity(static_cast<proto::logging::LogData_Severity>(4));
		data.set_sink("Crash");
		data.set_message(sanitizeField(reason));
		{
			std::lock_guard lock(logger->m.queue_mutex);
			logger->m.log_queue.push_back({.data = std::move(data), .trace = trace});
			++logger->m.enqueued;
		}

		// the sending thread owns the promise
		std::promise<void> done;
		auto finished = done.get_future();
		std::thread([promise = std::move(done)]() mutable {
			flush();
			promise.set_value();
		}).detach();
		finished.wait_for(std::chrono::milliseconds(1000));
	} catch (...) { (void)0; }
}

void Logger::flushSync(uint64_t target, std::chrono::steady_clock::time_point deadline) {
	ZoneScoped;

	// errors flush while the connection may not be up yet
	if (!m.connected.load(std::memory_order_acquire) || !m.socket.is_open()) {
		return;
	}

	while (m.sent.load(std::memory_order_relaxed) < target && std::chrono::steady_clock::now() < deadline) {
		// an empty batch reads as the goodbye
		const std::vector<uint8_t> batch = collectQueue(k_max_frame_bytes);
		if (batch.empty()) {
			return;
		}
		// a failed write is usually the send timeout so stop here
		if (writeFrame(batch)) {
			return;
		}
	}
}

}

extern "C" {
using namespace logging;

void toast_trace(const char* sink, const char* message, const char* file, unsigned line) noexcept {
	Logger::log(file, line, 0, sink, message);
}

void toast_info(const char* sink, const char* message, const char* file, unsigned line) noexcept {
	Logger::log(file, line, 1, sink, message);
}

void toast_warn(const char* sink, const char* message, const char* file, unsigned line) noexcept {
	Logger::log(file, line, 2, sink, message);
}

void toast_error(const char* sink, const char* message, const char* file, unsigned line) noexcept {
	Logger::log(file, line, 3, sink, message);
}

void toast_critical(const char* sink, const char* message, const char* file, unsigned line) noexcept {
	Logger::log(file, line, 4, sink, message);
}

void toast_log_invalidate_backtraces() noexcept {
	invalidateBacktraceCache();
}
}
