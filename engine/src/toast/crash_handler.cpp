#include "crash_handler.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <ffi/crash.h>
#include <print>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <atomic>
#include <crtdbg.h>
#include <csignal>
#include <cwchar>
#include <windows.h>
#endif

namespace toast::crash {

#ifdef _WIN32
namespace {

/// Mirrored by CrashKind in tools/crash_reporter/CrashTarget.cs
enum class Kind : uint8_t {
	exception = 0,
	crt_report = 1,
	terminate = 2,
	pure_call = 3,
	invalid_parameter = 4,
	abort = 5,
};

constexpr DWORD k_software_crash_code = 0xE0544F53;
constexpr DWORD k_status_heap_corruption = 0xC0000374;

/// Static so reporting never allocates since the heap may be what broke
struct SharedState {
	std::array<char, 4096> message {};
	EXCEPTION_RECORD record {};
	CONTEXT context {};
	EXCEPTION_POINTERS pointers {};
};

SharedState g_shared;
Kind g_kind = Kind::exception;
DWORD g_crashed_thread = 0;

std::array<wchar_t, MAX_PATH> g_reporter_path {};
std::array<wchar_t, MAX_PATH> g_dump_directory {};
std::array<wchar_t, 4 * MAX_PATH> g_command_line {};
std::array<char, 4096> g_scratch_message {};

std::atomic_bool g_installed {false};
std::atomic<DWORD> g_reporting_thread {0};

auto enabled() noexcept -> bool {
	std::array<wchar_t, 4> value {};
	const DWORD length = GetEnvironmentVariableW(L"TOAST_CRASH_HANDLER", value.data(), static_cast<DWORD>(value.size()));
	if (length > 0 && length < 4) {
		return value[0] != L'0';
	}
#ifdef NDEBUG
	return false;
#else
	return true;
#endif
}

auto fileExists(const wchar_t* path) noexcept -> bool {
	const DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

auto resolveReporter() noexcept -> bool {
	const DWORD override_length = GetEnvironmentVariableW(L"TOAST_CRASH_REPORTER", g_reporter_path.data(), MAX_PATH);
	if (override_length > 0 && override_length < MAX_PATH) {
		return fileExists(g_reporter_path.data());
	}

	HMODULE engine = nullptr;
	if (!GetModuleHandleExW(
	        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        reinterpret_cast<LPCWSTR>(&g_shared),
	        &engine
	    )) {
		return false;
	}

	std::array<wchar_t, MAX_PATH> directory {};
	const DWORD length = GetModuleFileNameW(engine, directory.data(), MAX_PATH);
	if (length == 0 || length >= MAX_PATH) {
		return false;
	}
	if (wchar_t* slash = wcsrchr(directory.data(), L'\\')) {
		*slash = L'\0';
	}

	std::array<wchar_t, MAX_PATH> candidate {};
	for (const wchar_t* relative : {L"\\..\\..\\crash_reporter\\crash_reporter.exe", L"\\crash_reporter\\crash_reporter.exe"}) {
		if (swprintf_s(candidate.data(), candidate.size(), L"%ls%ls", directory.data(), relative) < 0) {
			continue;
		}
		if (GetFullPathNameW(candidate.data(), MAX_PATH, g_reporter_path.data(), nullptr) > 0 && fileExists(g_reporter_path.data())) {
			return true;
		}
	}
	return false;
}

auto WINAPI launchReporter(LPVOID) -> DWORD {
	swprintf_s(
	    g_command_line.data(),
	    g_command_line.size(),
	    L"\"%ls\" --pid %lu --tid %lu --pointers 0x%llx --message 0x%llx --kind %d --dump-dir \"%ls\"",
	    g_reporter_path.data(),
	    GetCurrentProcessId(),
	    g_crashed_thread,
	    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&g_shared.pointers)),
	    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_shared.message.data())),
	    static_cast<int>(g_kind),
	    g_dump_directory.data()
	);

	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process {};
	if (!CreateProcessW(nullptr, g_command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
		// NOLINTNEXTLINE(modernize-use-std-print) fprintf since this runs mid-crash where the heap may be broken
		std::fprintf(stderr, "[Crash] Could not start the crash reporter (error %lu)\n", GetLastError());
		return 1;
	}

	// The reporter freezes this process and resumes it on exit
	WaitForSingleObject(process.hProcess, INFINITE);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return 0;
}

[[noreturn]]
void report(Kind kind, const EXCEPTION_POINTERS* pointers, const char* message) noexcept {
	const DWORD self = GetCurrentThreadId();
	DWORD expected = 0;
	if (!g_reporting_thread.compare_exchange_strong(expected, self)) {
		if (expected == self) {
			TerminateProcess(GetCurrentProcess(), k_software_crash_code);
		}
		// Parked so a second crash cannot race the report
		for (;;) {
			Sleep(INFINITE);
		}
	}

	g_kind = kind;
	g_crashed_thread = self;
	if (message != nullptr) {
		strncpy_s(g_shared.message.data(), g_shared.message.size(), message, _TRUNCATE);
	}

	if (pointers != nullptr) {
		g_shared.record = *pointers->ExceptionRecord;
		g_shared.context = *pointers->ContextRecord;
	} else {
		RtlCaptureContext(&g_shared.context);
		g_shared.record = EXCEPTION_RECORD {};
		g_shared.record.ExceptionCode = k_software_crash_code;
#if defined(_M_X64)
		g_shared.record.ExceptionAddress = reinterpret_cast<PVOID>(g_shared.context.Rip);
#endif
	}
	g_shared.pointers.ExceptionRecord = &g_shared.record;
	g_shared.pointers.ContextRecord = &g_shared.context;

	// Fresh thread since after a stack overflow this one has too little stack to start a process
	if (HANDLE thread = CreateThread(nullptr, 256 * 1024, launchReporter, nullptr, 0, nullptr)) {
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
	}

	TerminateProcess(GetCurrentProcess(), pointers != nullptr ? pointers->ExceptionRecord->ExceptionCode : k_software_crash_code);
	for (;;) {
		Sleep(INFINITE);
	}
}

/// Not crashes since .NET turns these into managed exceptions and some Windows APIs probe memory under SEH
auto isFaultSomeoneElseHandles(PVOID address) noexcept -> bool {
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
	        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        static_cast<LPCWSTR>(address),
	        &module
	    )) {
		return true;    // JIT compiled code
	}

	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
	if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0) {
		return true;    // managed assembly holding ReadyToRun code
	}

	for (const wchar_t* name : {L"coreclr.dll", L"clrjit.dll", L"clrgc.dll", L"kernelbase.dll", L"kernel32.dll"}) {
		if (GetModuleHandleW(name) == module) {
			return true;
		}
	}
	return false;
}

auto CALLBACK onVectoredException(EXCEPTION_POINTERS* pointers) -> LONG {
	switch (pointers->ExceptionRecord->ExceptionCode) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_BREAKPOINT:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case k_status_heap_corruption: break;
		default: return EXCEPTION_CONTINUE_SEARCH;
	}

	// Vectored handlers see every exception first so a debugger still gets its chance
	if (IsDebuggerPresent() || isFaultSomeoneElseHandles(pointers->ExceptionRecord->ExceptionAddress)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	report(Kind::exception, pointers, nullptr);
}

#ifdef _DEBUG
/// With a debugger attached the CRT dialog is the better tool
// NOLINTNEXTLINE(readability-non-const-parameter) signature fixed by _CRT_REPORT_HOOK
auto __cdecl onCrtReport(int type, char* message, int* return_value) -> int {
	(void)return_value;
	if (type == _CRT_WARN || IsDebuggerPresent()) {
		return FALSE;
	}
	report(Kind::crt_report, nullptr, message);
}

// NOLINTNEXTLINE(readability-non-const-parameter) signature fixed by _CRT_REPORT_HOOKW
auto __cdecl onCrtReportWide(int type, wchar_t* message, int* return_value) -> int {
	(void)return_value;
	if (type == _CRT_WARN || IsDebuggerPresent()) {
		return FALSE;
	}
	WideCharToMultiByte(
	    CP_UTF8, 0, message, -1, g_scratch_message.data(), static_cast<int>(g_scratch_message.size() - 1), nullptr, nullptr
	);
	report(Kind::crt_report, nullptr, g_scratch_message.data());
}
#endif

/// @note Terminate handlers are per thread so other threads end up in onAbortSignal() without the message
[[noreturn]]
void onTerminate() noexcept {
	const char* message = "std::terminate() was called without an active exception";
	if (const std::exception_ptr current = std::current_exception()) {
		try {
			std::rethrow_exception(current);
		} catch (const std::exception& e) {
			std::snprintf(g_scratch_message.data(), g_scratch_message.size(), "Unhandled C++ exception: %s", e.what());
			message = g_scratch_message.data();
		} catch (...) { message = "Unhandled C++ exception of a type not derived from std::exception"; }
	}

	if (IsDebuggerPresent()) {
		std::abort();
	}
	report(Kind::terminate, nullptr, message);
}

void __cdecl onPureCall() {
	if (!IsDebuggerPresent()) {
		report(Kind::pure_call, nullptr, "A pure virtual function was called");
	}
	std::abort();
}

void __cdecl onInvalidParameter(
    const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, std::uintptr_t reserved
) {
	(void)reserved;
	if (IsDebuggerPresent()) {
		__debugbreak();
		return;
	}

	// Release CRTs pass nulls
	std::array<wchar_t, 1024> text {};
	swprintf_s(
	    text.data(),
	    text.size(),
	    L"%ls in %ls (%ls:%u)",
	    expression != nullptr ? expression : L"?",
	    function != nullptr ? function : L"?",
	    file != nullptr ? file : L"?",
	    line
	);
	WideCharToMultiByte(
	    CP_UTF8, 0, text.data(), -1, g_scratch_message.data(), static_cast<int>(g_scratch_message.size() - 1), nullptr, nullptr
	);
	report(Kind::invalid_parameter, nullptr, g_scratch_message.data());
}

void __cdecl onAbortSignal(int signal) {
	(void)signal;
	if (!IsDebuggerPresent()) {
		report(Kind::abort, nullptr, "abort() was called");
	}
}

}
#endif

namespace {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4717)    // recursive on all control paths on purpose
#endif
void overflowStack(volatile std::uint64_t depth) {
	std::array<volatile char, 4096> padding {};
	padding[0] = static_cast<char>(depth);
	overflowStack(depth + 1);
	padding[1] = padding[0];    // after the call so it cannot become a loop
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}

void install() noexcept {
#ifdef _WIN32
	if (g_installed.exchange(true) || !enabled()) {
		return;
	}
	if (!resolveReporter()) {
		std::println(stderr, "[Crash] crash_reporter.exe was not found next to the engine; native crashes will not be reported");
		return;
	}

	// First in the chain so managed faults pass through untouched
	AddVectoredExceptionHandler(1, onVectoredException);
	std::set_terminate(onTerminate);
	_set_purecall_handler(onPureCall);
	_set_invalid_parameter_handler(onInvalidParameter);
	std::signal(SIGABRT, onAbortSignal);
#ifdef _DEBUG
	_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, onCrtReport);
	_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, onCrtReportWide);
#endif
#endif
}

void setDumpDirectory(const std::filesystem::path& directory) noexcept {
#ifdef _WIN32
	try {
		std::wstring text = std::filesystem::path(directory).make_preferred().lexically_normal().wstring();
		// A trailing backslash would escape the closing quote on the command line
		while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) {
			text.pop_back();
		}
		wcsncpy_s(g_dump_directory.data(), g_dump_directory.size(), text.c_str(), _TRUNCATE);
	} catch (...) { g_dump_directory[0] = L'\0'; }
#else
	(void)directory;
#endif
}

void triggerTestCrash(int kind) noexcept {
	switch (kind) {
		case 0: {
			volatile std::uintptr_t address = 0;
			*reinterpret_cast<int*>(static_cast<std::uintptr_t>(address)) = 1;
			break;
		}
		case 1: {
			std::vector<int> empty;
			volatile std::size_t index = 1;
			volatile int value = empty[index];
			(void)value;
			break;
		}
		case 2: {
			try {
				throw std::runtime_error("toast_crash_test: an exception nothing caught");
			} catch (...) { std::terminate(); }
			break;
		}
		case 3: overflowStack(0); break;
		case 4: std::abort();
		default: break;
	}
}

}

extern "C" {

void toast_crash_handler_install() noexcept {
	toast::crash::install();
}

void toast_crash_test(int kind) noexcept {
	toast::crash::triggerTestCrash(kind);
}
}
