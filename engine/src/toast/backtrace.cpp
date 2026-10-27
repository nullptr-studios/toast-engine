#include "backtrace.hpp"

#include "log.hpp"

#include <algorithm>
#include <cpptrace/basic.hpp>
#include <cpptrace/utils.hpp>
#include <format>
#include <mutex>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <unordered_set>
#include <windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace logging {

namespace {

constexpr std::size_t k_kept_frames = 16;

std::mutex g_cache_mutex;
std::unordered_map<std::uintptr_t, std::string> g_cache;
std::uint64_t g_cache_generation = 0;
#ifdef _WIN32
std::unordered_set<std::uintptr_t> g_symbol_loaded_modules;
#endif

struct Module {
	std::string path;
	std::uintptr_t base = 0;
};

auto moduleOf(std::uintptr_t address) -> Module {
#ifdef _WIN32
	HMODULE handle = nullptr;
	if (!GetModuleHandleExA(
	        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        reinterpret_cast<LPCSTR>(address),
	        &handle
	    )) {
		return {};
	}
	std::array<char, MAX_PATH> path {};
	const DWORD length = GetModuleFileNameA(handle, path.data(), MAX_PATH);
	if (length == 0) {
		return {};
	}
	return {.path = std::string(path.data(), length), .base = reinterpret_cast<std::uintptr_t>(handle)};
#else
	Dl_info info {};
	if (dladdr(reinterpret_cast<const void*>(address), &info) == 0 || info.dli_fname == nullptr) {
		return {};
	}
	return {.path = info.dli_fname, .base = reinterpret_cast<std::uintptr_t>(info.dli_fbase)};
#endif
}

#ifdef _WIN32
auto loadSymbolsOnce(const Module& module) -> bool {
	{
		std::lock_guard lock(g_cache_mutex);
		if (!g_symbol_loaded_modules.insert(module.base).second) {
			return false;
		}
	}
	try {
		cpptrace::load_symbols_for_file(module.path);
		return true;
	} catch (...) { return false; }
}
#endif

auto resolveFrame(std::uintptr_t address) -> cpptrace::stacktrace_frame {
	cpptrace::stacktrace resolved = cpptrace::raw_trace {{address}}.resolve();
	if (resolved.frames.empty()) {
		return {};
	}
	return std::move(resolved.frames.front());
}

auto resolveLine(std::uintptr_t address) -> std::string {
	const Module module = moduleOf(address);
	if (module.path.empty()) {
		return std::format("{:#x}", address);
	}
	std::string module_location = std::format("{}+{:#x}", _detail::getOnlyName(module.path), address - module.base);

	cpptrace::stacktrace_frame frame = resolveFrame(address);
#ifdef _WIN32
	if (frame.symbol.empty() && loadSymbolsOnce(module)) {
		frame = resolveFrame(address);
	}
#endif
	if (frame.symbol.empty()) {
		return module_location;
	}

	const std::string function = cpptrace::prune_symbol(frame.symbol);
	if (frame.filename.empty() || !frame.line.has_value()) {
		return std::format("{} ({})", function, module_location);
	}
	return std::format("{} ({}:{})", function, _detail::getOnlyName(frame.filename), frame.line.value());
}

auto cachedLine(std::uintptr_t address) -> std::string {
	std::uint64_t generation = 0;
	{
		std::lock_guard lock(g_cache_mutex);
		if (auto it = g_cache.find(address); it != g_cache.end()) {
			return it->second;
		}
		generation = g_cache_generation;
	}

	std::string line = resolveLine(address);

	std::lock_guard lock(g_cache_mutex);
	if (generation == g_cache_generation) {
		g_cache.emplace(address, line);
	}
	return line;
}

auto isLoggerFrame(std::string_view line) -> bool {
	const std::string_view function = line.substr(0, line.find(' '));
	if (function.starts_with("logging::") || function.starts_with("Logger::")) {
		return true;
	}
	return function.starts_with("toast_") && function.find_first_of(".+") == std::string_view::npos;
}

}

CPPTRACE_FORCE_NO_INLINE
auto captureBacktrace(std::size_t skip) noexcept -> RawBacktrace {
	RawBacktrace trace;
#ifdef _WIN32
	std::array<void*, RawBacktrace::k_capture_depth> addresses {};
	const USHORT count =
	    RtlCaptureStackBackTrace(static_cast<DWORD>(skip + 1), static_cast<DWORD>(addresses.size()), addresses.data(), nullptr);
	for (USHORT i = 0; i < count; ++i) {
		trace.frames[i] = reinterpret_cast<std::uintptr_t>(addresses[i]) - 1;
	}
	trace.count = static_cast<std::uint8_t>(count);
#else
	trace.count = static_cast<std::uint8_t>(cpptrace::safe_generate_raw_trace(trace.frames.data(), trace.frames.size(), skip + 1));
	if (trace.count == 0) {
		constexpr std::size_t k_max_skip = 8;
		const std::size_t skipped = std::min(skip, k_max_skip) + 1;    // + this function
		std::array<void*, k_max_skip + 1 + RawBacktrace::k_capture_depth> addresses {};
		const int captured = backtrace(addresses.data(), static_cast<int>(skipped + RawBacktrace::k_capture_depth));
		for (int i = static_cast<int>(skipped); i < captured; ++i) {
			// Return addresses point past the call, one back lands inside it as cpptrace's own capture does
			trace.frames[trace.count++] = reinterpret_cast<std::uintptr_t>(addresses[static_cast<std::size_t>(i)]) - 1;
		}
	}
#endif
	return trace;
}

auto formatBacktrace(const RawBacktrace& trace) -> std::string {
	std::string text;
	try {
		const std::size_t count = std::min<std::size_t>(trace.count, trace.frames.size());
		std::size_t kept = 0;
		bool leading = true;
		for (std::size_t i = 0; i < count && kept < k_kept_frames; ++i) {
			const std::string line = cachedLine(trace.frames[i]);
			if (leading && isLoggerFrame(line)) {
				continue;
			}
			leading = false;
			if (kept > 0) {
				text += '\n';
			}
			text += line;
			++kept;
		}
	} catch (...) { (void)0; }    // keep whatever resolved
	return text;
}

void warmUpBacktrace() noexcept {
	try {
		const RawBacktrace trace = captureBacktrace();
		if (trace.count > 0) {
			[[maybe_unused]]
			auto line = cachedLine(trace.frames[0]);
		}
	} catch (...) { (void)0; }
}

void invalidateBacktraceCache() noexcept {
	std::lock_guard lock(g_cache_mutex);
	g_cache.clear();
	++g_cache_generation;
#ifdef _WIN32
	g_symbol_loaded_modules.clear();
#endif
}

}
