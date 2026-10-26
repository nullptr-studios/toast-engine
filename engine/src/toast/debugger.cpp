#include "log.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <ctime>
#include <unistd.h>
#ifdef __linux__
#include <fcntl.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#endif

namespace logging {

namespace {

/// how long an answer is reused
constexpr int64_t k_cache_milliseconds = 250;

/// 0 until read then 1 allows breaking and 2 does not
std::atomic<uint8_t> g_opt_out_state {0};
std::atomic<bool> g_cached_attached {false};
std::atomic<int64_t> g_cache_expires_ms {0};

auto readOptOut() noexcept -> bool {
#ifdef _WIN32
	std::array<char, 4> value {};
	const DWORD length = GetEnvironmentVariableA("TOAST_NO_BREAK", value.data(), static_cast<DWORD>(value.size()));
	return length == 1 && value[0] == '1';
#else
	const char* value = std::getenv("TOAST_NO_BREAK");
	return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
}

auto breakingDisabled() noexcept -> bool {
	uint8_t state = g_opt_out_state.load(std::memory_order_relaxed);
	if (state == 0) {
		state = readOptOut() ? 2 : 1;
		g_opt_out_state.store(state, std::memory_order_relaxed);
	}
	return state == 2;
}

/// reads the environment on load so handlers never call getenv
[[maybe_unused]]
const bool g_opt_out_primed = breakingDisabled();

auto monotonicMilliseconds() noexcept -> int64_t {
#ifdef _WIN32
	return static_cast<int64_t>(GetTickCount64());
#else
	timespec now {};
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (static_cast<int64_t>(now.tv_sec) * 1000) + (static_cast<int64_t>(now.tv_nsec) / 1'000'000);
#endif
}

#ifdef __linux__
/// @param status start of /proc/self/status
auto hasTracer(std::string_view status) noexcept -> bool {
	constexpr std::string_view key = "\nTracerPid:";
	const std::size_t at = status.find(key);
	if (at == std::string_view::npos) {
		return false;
	}
	for (std::size_t i = at + key.size(); i < status.size(); ++i) {
		const char c = status[i];
		if (c == ' ' || c == '\t') {
			continue;
		}
		return c >= '1' && c <= '9';
	}
	return false;
}
#endif

/// asks the OS and is safe to call from a signal handler
auto queryDebugger() noexcept -> bool {
#if defined(_WIN32)
	return IsDebuggerPresent() != FALSE;
#elif defined(__linux__)
	const int saved_errno = errno;
	bool attached = false;
	const int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		// TracerPid is in the first few lines
		std::array<char, 512> buffer;
		ssize_t length = 0;
		do {
			length = read(fd, buffer.data(), buffer.size());
		} while (length < 0 && errno == EINTR);
		close(fd);
		attached = length > 0 && hasTracer({buffer.data(), static_cast<std::size_t>(length)});
	}
	errno = saved_errno;
	return attached;
#elif defined(__APPLE__)
	const int saved_errno = errno;
	std::array<int, 4> mib = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
	kinfo_proc info {};
	std::size_t size = sizeof(info);
	const bool queried = sysctl(mib.data(), static_cast<u_int>(mib.size()), &info, &size, nullptr, 0) == 0;
	const bool attached = queried && (info.kp_proc.p_flag & P_TRACED) != 0;
	errno = saved_errno;
	return attached;
#else
	return false;
#endif
}
}

auto _detail::debuggerAttached() noexcept -> bool {
	if (breakingDisabled()) {
		return false;
	}

	const int64_t now = monotonicMilliseconds();
	if (now < g_cache_expires_ms.load(std::memory_order_relaxed)) {
		return g_cached_attached.load(std::memory_order_relaxed);
	}

	const bool attached = queryDebugger();
	g_cached_attached.store(attached, std::memory_order_relaxed);
	g_cache_expires_ms.store(now + k_cache_milliseconds, std::memory_order_relaxed);
	return attached;
}

}
