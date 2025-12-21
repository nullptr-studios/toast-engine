// src/Core/Log.cpp
#include <Engine/Core/Log.hpp>

// clang-format off
// private: spdlog includes
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/logger.h>
#include <spdlog/fmt/ostr.h>

#include <ctime>
#include <iomanip>
#include <memory>
#include <source_location>
// clang-format on

namespace toast {

// private statics (file-local)
static std::shared_ptr<spdlog::logger> s_engineLogger;
static std::shared_ptr<spdlog::logger> s_clientLogger;

static spdlog::level::level_enum ToSpdLevel(Log::Level lvl) {
	switch (lvl) {
		case Log::Level::Trace: return spdlog::level::trace;
		case Log::Level::Info: return spdlog::level::info;
		case Log::Level::Warning: return spdlog::level::warn;
		case Log::Level::Error: return spdlog::level::err;
		case Log::Level::Critical: return spdlog::level::critical;
		case Log::Level::Off: return spdlog::level::off;
	}
	return spdlog::level::info;
}

void Log::Init() {
	if (s_engineLogger && s_clientLogger) {
		return;
	}

	// sinks
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_st>();
	console_sink->set_pattern("%^[%n] %v%$");

	std::ostringstream filename;
	auto t = std::time(nullptr);
	filename << "logs/" << std::put_time(std::localtime(&t), "%Y-%m-%d_%H-%M-%S") << ".log";
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_st>(filename.str(), true);
	file_sink->set_pattern("[%Y-%m-%d %T.%e] [%l] [%n] %v");

	// engine logger
	s_engineLogger = std::make_shared<spdlog::logger>("TOAST", spdlog::sinks_init_list { console_sink, file_sink });
	s_engineLogger->set_level(spdlog::level::trace);
	spdlog::register_logger(s_engineLogger);

	// client logger
	s_clientLogger = std::make_shared<spdlog::logger>("GAME", spdlog::sinks_init_list { console_sink, file_sink });
	s_clientLogger->set_level(spdlog::level::trace);
	spdlog::register_logger(s_clientLogger);
}

void Log::EngineLog(Level lvl, std::string_view msg) {
	if (!s_engineLogger) {
		Init();
	}
	s_engineLogger->log(ToSpdLevel(lvl), "{}", msg);
}

void Log::ClientLog(Level lvl, std::string_view msg) {
	if (!s_clientLogger) {
		Init();
	}
	s_clientLogger->log(ToSpdLevel(lvl), "{}", msg);
}

void Log::ChangeEngineLevel(Level lvl) {
	if (!s_engineLogger) {
		Init();
	}
	s_engineLogger->set_level(ToSpdLevel(lvl));
}

void Log::ChangeClientLevel(Level lvl) {
	if (!s_clientLogger) {
		Init();
	}
	s_clientLogger->set_level(ToSpdLevel(lvl));
}

}

void ToastException::ShowDialog() const { }
