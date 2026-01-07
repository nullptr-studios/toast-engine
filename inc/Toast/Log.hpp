#pragma once

#include <Toast/Profiler.hpp>
#include <cassert>
#include <format>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>

namespace toast {

class Log {
public:
	static void Init();

	enum Level : char {
		Trace = 0,
		Info,
		Warning,
		Error,
		Critical,
		Off
	};

	// Non-templated thin sinks implemented in Log.cpp (private spdlog usage).
	static void EngineLog(Level lvl, std::string_view msg);
	static void ClientLog(Level lvl, std::string_view msg);

	template<typename... Args>
	static std::string Format(std::string_view fmt, Args&&... args) {
		return std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
	}

	// Convenience: format + log helpers that forward to EngineLog/ClientLog
	template<typename... Args>
	static void EngineFmt(Level lvl, std::string_view fmt, Args&&... args) {
		auto s = Format(fmt, std::forward<Args>(args)...);
		EngineLog(lvl, s);
	}

	template<typename... Args>
	static void ClientFmt(Level lvl, std::string_view fmt, Args&&... args) {
		auto s = Format(fmt, std::forward<Args>(args)...);
		ClientLog(lvl, s);
	}

	// Change level helpers
	static void ChangeEngineLevel(Level lvl);
	static void ChangeClientLevel(Level lvl);
};

}    // namespace toast

#pragma region Macros
// ---------------------- Engine Macros ----------------------

// ASSERT macros (engine)
#define TOAST_ASSERT(condition, ...)                                       \
	do {                                                                     \
		if (!(condition)) {                                                    \
			::toast::Log::EngineFmt(::toast::Log::Level::Critical, __VA_ARGS__); \
			assert(condition);                                                   \
		}                                                                      \
	} while (0)

// Engine logging macros: format, log, profile
#define TOAST_ERROR(...)                                      \
	do {                                                        \
		auto msg = std::format(__VA_ARGS__);                      \
		::toast::Log::EngineLog(::toast::Log::Level::Error, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0xDC143C);     \
	} while (0)

#define TOAST_WARN(...)                                         \
	do {                                                          \
		auto msg = std::format(__VA_ARGS__);                        \
		::toast::Log::EngineLog(::toast::Log::Level::Warning, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0xFFD700);       \
	} while (0)

#define TOAST_INFO(...)                                      \
	do {                                                       \
		auto msg = std::format(__VA_ARGS__);                     \
		::toast::Log::EngineLog(::toast::Log::Level::Info, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0x7CFC00);    \
	} while (0)

#define TOAST_TRACE(...)                                      \
	do {                                                        \
		auto msg = std::format(__VA_ARGS__);                      \
		::toast::Log::EngineLog(::toast::Log::Level::Trace, msg); \
		PROFILE_MESSAGE(msg.c_str(), msg.size());                 \
	} while (0)

// ---------------------- Client macros ----------------------

#define CLIENT_ASSERT(condition, ...)                                      \
	do {                                                                     \
		if (!(condition)) {                                                    \
			::toast::Log::ClientFmt(::toast::Log::Level::Critical, __VA_ARGS__); \
			assert(condition);                                                   \
		}                                                                      \
	} while (0)

#define CLIENT_ERROR(...)                                     \
	do {                                                        \
		auto msg = std::format(__VA_ARGS__);                      \
		::toast::Log::ClientLog(::toast::Log::Level::Error, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0xDC143C);     \
	} while (0)

#define CLIENT_WARN(...)                                        \
	do {                                                          \
		auto msg = std::format(__VA_ARGS__);                        \
		::toast::Log::ClientLog(::toast::Log::Level::Warning, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0xFFD700);       \
	} while (0)

#define CLIENT_INFO(...)                                     \
	do {                                                       \
		auto msg = std::format(__VA_ARGS__);                     \
		::toast::Log::ClientLog(::toast::Log::Level::Info, msg); \
		PROFILE_MESSAGE_C(msg.c_str(), msg.size(), 0x7CFC00);    \
	} while (0)

#define CLIENT_TRACE(...)                                     \
	do {                                                        \
		auto msg = std::format(__VA_ARGS__);                      \
		::toast::Log::ClientLog(::toast::Log::Level::Trace, msg); \
		PROFILE_MESSAGE(msg.c_str(), msg.size());                 \
	} while (0)

#ifdef NDEBUG
#undef TOAST_ASSERT
#undef CLIENT_ASSERT
#define TOAST_ASSERT(condition, ...) \
	do {                               \
		(void)condition;                 \
	} while (0)
#define CLIENT_ASSERT(condition, ...) \
	do {                                \
		(void)condition;                  \
	} while (0)
#endif

#pragma endregion

// Exception that logs using the macro
class ToastException : public std::exception {
	std::string m_message;

public:
	ToastException(const std::string& message, std::source_location loc = std::source_location::current()) : m_message(message) {
		m_message += "\n\nIn file: " + std::string(loc.file_name());
		m_message += "\nAt line: " + std::to_string(loc.line());
		m_message += "\nIn function: " + std::string(loc.function_name());
		TOAST_ERROR("Exception: {0}", m_message);
		ShowDialog();
	}

	[[nodiscard]]
	const char* what() const noexcept override {
		return m_message.c_str();
	}

private:
	void ShowDialog() const;
};
