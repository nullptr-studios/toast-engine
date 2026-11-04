#ifndef _WIN32

#include "backtrace.hpp"
#include "crash_handler.hpp"
#include "log.hpp"
#include "logger.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <string_view>
#include <thread>
#include <unistd.h>

// flushes the logs when the process dies on a fatal signal or terminate
namespace toast::crash {

namespace {

constexpr std::array<int, 5> k_signals {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};
constexpr std::size_t k_alt_stack_size = 128 * 1024;
constexpr int k_wait_milliseconds = 1000;

struct CrashInfo {
	int signal = 0;    ///< 0 for terminate
	void* address = nullptr;
	logging::RawBacktrace trace;
};

CrashInfo g_info;
std::array<char, 320> g_terminate_message {};
std::array<struct sigaction, k_signals.size()> g_previous {};
std::array<char, k_alt_stack_size> g_alt_stack {};
std::array<int, 2> g_wake_pipe {-1, -1};
std::atomic_bool g_installed {false};
std::atomic_bool g_claimed {false};    ///< the first crash owns the report
std::atomic_bool g_done {false};

auto slotOf(int signal) noexcept -> int {
	for (std::size_t i = 0; i < k_signals.size(); ++i) {
		if (k_signals[i] == signal) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

auto signalName(int signal) noexcept -> const char* {
	switch (signal) {
		case SIGABRT: return "SIGABRT";
		case SIGSEGV: return "SIGSEGV";
		case SIGBUS: return "SIGBUS";
		case SIGILL: return "SIGILL";
		case SIGFPE: return "SIGFPE";
		default: return "signal";
	}
}

/// faults in managed images belong to the host
auto isForeignImage(const char* path) noexcept -> bool {
	constexpr std::array<const char*, 7> needles {
	  "libcoreclr", "libclrjit", "libclrgc", "libhostfxr", "libhostpolicy", "libSystem.", ".dll"
	};
	for (const char* needle : needles) {
		if (std::strstr(path, needle) != nullptr) {
			return true;
		}
	}
	return false;
}

/// true when the interrupted code is native
auto faultIsNative(const logging::RawBacktrace& trace) noexcept -> bool {
	for (std::size_t i = 1; i <= 2 && i < trace.count; ++i) {
		Dl_info module {};
		if (dladdr(reinterpret_cast<void*>(trace.frames[i]), &module) != 0 && module.dli_fname != nullptr) {
			return !isForeignImage(module.dli_fname);
		}
	}
	return false;
}

auto hasPreviousHandler(int slot) noexcept -> bool {
	const struct sigaction& previous = g_previous[static_cast<std::size_t>(slot)];
	if ((previous.sa_flags & SA_SIGINFO) != 0) {
		return previous.sa_sigaction != nullptr;
	}
	return previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN;
}

void callPrevious(int slot, int signal, siginfo_t* info, void* context) noexcept {
	const struct sigaction& previous = g_previous[static_cast<std::size_t>(slot)];
	if ((previous.sa_flags & SA_SIGINFO) != 0) {
		previous.sa_sigaction(signal, info, context);
	} else {
		previous.sa_handler(signal);
	}
}

void waitForFlush() noexcept {
	timespec pause {};
	pause.tv_sec = 0;
	pause.tv_nsec = 1'000'000;
	for (int waited = 0; waited < k_wait_milliseconds && !g_done.load(std::memory_order_acquire); ++waited) {
		nanosleep(&pause, nullptr);
	}
}

/// hands the crash to the helper thread and waits
void requestFlush(int signal, void* address, const logging::RawBacktrace& trace) noexcept {
	if (!g_claimed.exchange(true)) {
		g_info.signal = signal;
		g_info.address = address;
		g_info.trace = trace;
		const char byte = 1;
		[[maybe_unused]]
		const ssize_t written = write(g_wake_pipe[1], &byte, 1);
	}
	waitForFlush();
}

/// blocks until a crash wakes it
void helperMain() noexcept {
	char byte = 0;
	ssize_t got = 0;
	do {
		got = read(g_wake_pipe[0], &byte, 1);
	} while (got < 0 && errno == EINTR);
	if (got != 1) {
		return;
	}

	std::array<char, 320> reason {};
	if (g_info.signal == 0) {
		std::snprintf(reason.data(), reason.size(), "%s", g_terminate_message.data());
	} else if (g_info.signal == SIGABRT) {
		std::snprintf(reason.data(), reason.size(), "Engine crashed: SIGABRT (abort() was called)");
	} else {
		std::snprintf(reason.data(), reason.size(), "Engine crashed: %s at %p", signalName(g_info.signal), g_info.address);
	}
	logging::Logger::crashFlush(reason.data(), g_info.trace);
	g_done.store(true, std::memory_order_release);
}

/// restores the default action and raises the signal again
void dieWithSignal(int signal) noexcept {
	std::signal(signal, SIG_DFL);
	raise(signal);
}

void onFatalSignal(int signal, siginfo_t* info, void* context) {
	const int saved_errno = errno;
	const int slot = slotOf(signal);

	// the debugger has the original fault
	if (logging::_detail::debuggerAttached()) {
		if (slot >= 0 && hasPreviousHandler(slot)) {
			callPrevious(slot, signal, info, context);
		} else {
			dieWithSignal(signal);
		}
		errno = saved_errno;
		return;
	}

	const logging::RawBacktrace trace = logging::captureBacktrace();
	if (signal != SIGABRT && slot >= 0 && hasPreviousHandler(slot) && !faultIsNative(trace)) {
		callPrevious(slot, signal, info, context);
		errno = saved_errno;
		return;
	}

	requestFlush(signal, info != nullptr ? info->si_addr : nullptr, trace);
	dieWithSignal(signal);
	errno = saved_errno;
}

[[noreturn]]
void onTerminate() noexcept {
	// not a signal context so this may allocate
	if (const std::exception_ptr current = std::current_exception()) {
		try {
			std::rethrow_exception(current);
		} catch (const std::exception& e) {
			std::snprintf(
			    g_terminate_message.data(), g_terminate_message.size(), "Engine crashed: unhandled C++ exception: %s", e.what()
			);
		} catch (...) {
			std::snprintf(
			    g_terminate_message.data(), g_terminate_message.size(), "Engine crashed: unhandled C++ exception of an unknown type"
			);
		}
	} else {
		std::snprintf(
		    g_terminate_message.data(),
		    g_terminate_message.size(),
		    "Engine crashed: std::terminate() was called without an active exception"
		);
	}

	requestFlush(0, nullptr, logging::captureBacktrace());
	// abort raises SIGABRT and the handler finds the crash reported
	std::abort();
}

}

void installPosix() noexcept {
	if (g_installed.exchange(true)) {
		return;
	}

	// first use allocates so do it outside a handler
	[[maybe_unused]]
	const logging::RawBacktrace warm = logging::captureBacktrace();
	[[maybe_unused]]
	const bool attached = logging::_detail::debuggerAttached();

	if (pipe(g_wake_pipe.data()) != 0) {
		return;
	}
	for (const int descriptor : g_wake_pipe) {
		fcntl(descriptor, F_SETFD, FD_CLOEXEC);
	}

	try {
		std::thread(helperMain).detach();
	} catch (...) { return; }

	// per thread so this one survives a stack overflow
	stack_t alt_stack {};
	alt_stack.ss_sp = g_alt_stack.data();
	alt_stack.ss_size = g_alt_stack.size();
	sigaltstack(&alt_stack, nullptr);

	for (std::size_t i = 0; i < k_signals.size(); ++i) {
		struct sigaction action {};
		action.sa_sigaction = onFatalSignal;
		sigemptyset(&action.sa_mask);
		action.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigaction(k_signals[i], &action, &g_previous[i]);
	}
	std::set_terminate(onTerminate);
}

}

#endif
