/**
 * @file main.cpp
 * @author Xein
 * @date 25 Oct 2026
 */

#include "server.hpp"
#include "storage.hpp"
#include "upload.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr std::uint16_t tui_port = 12801;

struct Args {
	/// engine port
	std::uint16_t port = 12800;
	/// keep running after the engine disconnects
	bool infinite = false;
};

/// closed on shutdown
log_server::LogStorage* g_storage = nullptr;

void printUsage(std::FILE* out) {
	std::fputs("Usage: log_server [OPTIONS]\n\nOptions:\n      --port <PORT>  Port for the engine to connect to [default: 12800]\n      --infinite     Keep server running even after engine disconnects\n  -h, --help         Print help\n", out);
}

[[noreturn]] void usageError(const std::string& message) {
	std::fprintf(stderr, "error: %s\n\n", message.c_str());
	printUsage(stderr);
	std::exit(2);
}

std::uint16_t parsePort(std::string_view text) {
	unsigned value = 0;
	const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (ec != std::errc() || end != text.data() + text.size() || value > 65535) {
		usageError("invalid value '" + std::string(text) + "' for '--port <PORT>'");
	}
	return static_cast<std::uint16_t>(value);
}

Args parseArgs(int argc, char** argv) {
	Args args;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg == "--infinite") {
			args.infinite = true;
		} else if (arg == "--port") {
			if (i + 1 >= argc) {
				usageError("a value is required for '--port <PORT>' but none was supplied");
			}
			args.port = parsePort(argv[++i]);
		} else if (arg.starts_with("--port=")) {
			args.port = parsePort(arg.substr(7));
		} else if (arg == "-h" || arg == "--help") {
			printUsage(stdout);
			std::exit(0);
		} else {
			usageError("unexpected argument '" + std::string(arg) + "' found");
		}
	}
	return args;
}

/// arguments as utf8
std::vector<std::string> commandLineArgs(int argc, char** argv) {
	std::vector<std::string> args;
#ifdef _WIN32
	(void)argc;
	(void)argv;
	int count = 0;
	wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
	if (wide != nullptr) {
		for (int i = 1; i < count; ++i) {
			const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
			std::string text(size > 0 ? static_cast<std::size_t>(size - 1) : 0, char{});
			WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
			args.push_back(std::move(text));
		}
		LocalFree(wide);
	}
#else
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}
#endif
	return args;
}

/// closes the session file and exits
[[noreturn]] void shutdownNow() {
	log_server::finishAndUpload(*g_storage);
	std::_Exit(0);
}

#ifdef _WIN32
BOOL WINAPI onConsoleEvent(DWORD event) {
	if (event == CTRL_C_EVENT || event == CTRL_SHUTDOWN_EVENT) {
		shutdownNow();
	}
	return FALSE;
}
#endif

}  // namespace

int main(int argc, char** argv) {
	// the detached child that uploads a session
	if (const auto upload = log_server::parseUploadArgs(commandLineArgs(argc, argv))) {
		log_server::runUpload(*upload);
		return 0;
	}

	const Args args = parseArgs(argc, argv);

	log_server::LogStorage storage;
	g_storage = &storage;
	log_server::Server server(storage);

	// close the session file on an unhandled exception
	std::set_terminate([] {
		log_server::finishAndUpload(*g_storage);
		std::abort();
	});

	asio::io_context context;

	// close the session file when asked to stop
#ifdef _WIN32
	SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#else
	asio::signal_set signals(context, SIGINT, SIGTERM, SIGHUP);
	signals.async_wait([](const asio::error_code& ec, int) {
		if (!ec) {
			shutdownNow();
		}
	});
#endif

	asio::co_spawn(context, server.runTuiListener(tui_port), [](std::exception_ptr error) {
		if (error) {
			try {
				std::rethrow_exception(error);
			} catch (const std::exception& e) {
				log_server::note(std::string("TUI Listener failed: ") + e.what());
			}
		}
	});

	int exit_code = 0;
	asio::co_spawn(context, server.runEngineListener(args.port, args.infinite), [&](std::exception_ptr error) {
		if (error) {
			try {
				std::rethrow_exception(error);
			} catch (const std::exception& e) {
				std::fprintf(stderr, "Error: %s\n", e.what());
			}
			exit_code = 1;
			context.stop();
		}
	});

	context.run();
	log_server::finishAndUpload(storage);
	return exit_code;
}
