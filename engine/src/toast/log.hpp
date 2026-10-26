/**
 * @file log.hpp
 * @author Xein
 * @date 17 Mar 2026
 * @brief Public logging interface for the engine
 */

#pragma once
#include "export.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <utility>

/// @internal
namespace logging::_detail {
constexpr auto getOnlyName(std::string_view path) -> std::string_view {
	size_t last_slash = path.find_last_of("\\/");
	if (last_slash == std::string_view::npos) {
		return path;
	}
	return path.substr(last_slash + 1);
}

void TOAST_API
    log(uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message);

/// details is the long body of a log
TOAST_API void
    log(uint8_t severity, std::string_view file_name, unsigned line_number, std::string_view sink, std::string_view message,
		    std::string_view details);

/// true while a debugger is attached
TOAST_API auto debuggerAttached() noexcept -> bool;
}

namespace logging {
/// same values as the proto severity
enum class Severity : uint8_t {
	trace = 0,
	info = 1,
	warning = 2,
	error = 3,
	critical = 4,
};

/// first line and the rest of a text
constexpr auto splitFirstLine(std::string_view text) -> std::pair<std::string_view, std::string_view> {
	const size_t newline = text.find('\n');
	if (newline == std::string_view::npos) {
		return {text, {}};
	}
	std::string_view first = text.substr(0, newline);
	if (!first.empty() && first.back() == '\r') {
		first.remove_suffix(1);
	}
	return {first, text.substr(newline + 1)};
}

/// Sends every queued log now for paths about to abort the process
TOAST_API void flush() noexcept;

/// call after loading or freeing a library
TOAST_API void invalidateBacktraceCache() noexcept;
}

#define TOAST_FILE_NAME ::logging::_detail::getOnlyName(__FILE__)

/// breaks on the line that expands it
#if defined(_MSC_VER)
#define TOAST_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
#define TOAST_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#define TOAST_DEBUG_BREAK() __asm__ volatile("int3")
#elif defined(__GNUC__) && defined(__aarch64__)
#define TOAST_DEBUG_BREAK() __asm__ volatile("brk #0xf000")
#else
#include <csignal>
#define TOAST_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

/// does nothing without TOAST_BREAK_ON_ERROR
#ifdef TOAST_BREAK_ON_ERROR
#define TOAST_BREAK_IF_ATTACHED()                 \
	do {                                            \
		if (::logging::_detail::debuggerAttached()) { \
			TOAST_DEBUG_BREAK();                        \
		}                                             \
	} while (0)
#else
#define TOAST_BREAK_IF_ATTACHED() ((void)0)
#endif

#ifdef DEBUG
/// tracy only gets the summary
#define TOAST_LOG_TRACY(severity, message)                                                                             \
	do {                                                                                                                 \
		constexpr uint32_t red = 0xFF0000;                                                                                 \
		constexpr uint32_t green = 0x00FF00;                                                                               \
		constexpr uint32_t yellow = red | green; /* i love this so much -x */                                              \
		const std::string_view toast_log_text_ = (message);                                                                \
		switch (severity) {                                                                                                \
			case ::logging::Severity::critical:                                                                              \
			case ::logging::Severity::error: TracyMessageC(toast_log_text_.data(), toast_log_text_.size(), red); break;      \
			case ::logging::Severity::warning: TracyMessageC(toast_log_text_.data(), toast_log_text_.size(), yellow); break; \
			case ::logging::Severity::info: TracyMessageC(toast_log_text_.data(), toast_log_text_.size(), green); break;     \
			default: break;                                                                                                  \
		}                                                                                                                  \
	} while (0)
#else
#define TOAST_LOG_TRACY(severity, message) ((void)0)
#endif

/// every log macro ends up here
#define TOAST_LOG_CORE(severity, sink, message, details)                                                        \
	do {                                                                                                          \
		const ::logging::Severity toast_log_severity_ = (severity);                                                 \
		const auto& toast_log_message_ = (message);                                                                 \
		::logging::_detail::log(                                                                                    \
		    static_cast<uint8_t>(toast_log_severity_), TOAST_FILE_NAME, __LINE__, sink, toast_log_message_, details \
		);                                                                                                          \
		TOAST_LOG_TRACY(toast_log_severity_, toast_log_message_);                                                   \
		if (toast_log_severity_ >= ::logging::Severity::error) {                                                    \
			TOAST_BREAK_IF_ATTACHED();                                                                                \
		}                                                                                                           \
	} while (0)

#define TOAST_LOG_IMPL(severity, sink, ...)                                       \
	do {                                                                            \
		const std::string toast_log_impl_message_ = std::format(__VA_ARGS__);         \
		TOAST_LOG_CORE(severity, sink, toast_log_impl_message_, std::string_view {}); \
	} while (0)

/**
 * @param severity can be decided at runtime
 * @param sink class logging the message
 * @param message short summary
 * @param details long body
 * @brief Writes a log with a body
 */
#define TOAST_MESSAGE(severity, sink, message, details)              \
	do {                                                               \
		const ::logging::Severity toast_message_severity_ = (severity);  \
		TOAST_LOG_CORE(toast_message_severity_, sink, message, details); \
		if (toast_message_severity_ == ::logging::Severity::critical) {  \
			std::abort();                                                  \
		}                                                                \
	} while (0)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Writes a message to the log console
 *
 * A trace means "any text that should be logged". There can be multiple importances
 * of traces but anything that is not a really important message falls into this category
 *
 * If you are logging something and you don't know if reading this log will be meaningful in
 * half an hour, or even a week, then it is a trace
 *
 * If you will stop caring about this log as soon as you finish working on your task, then
 * it is a trace
 */
#define TOAST_TRACE(sink, ...) TOAST_LOG_IMPL(::logging::Severity::trace, sink, __VA_ARGS__)

/**
 * @brief logs an information message
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Indicates that a meaningful action has happened
 *
 * Information messages should mark a before and after when reading a log;
 * if you are not sure if your message is important enough, then it's probably
 * not an info log
 */
#define TOAST_INFO(sink, ...) TOAST_LOG_IMPL(::logging::Severity::info, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Indicates possible code missuse
 *
 * Something didn't happen as expected but should not be a major problem.
 * It can potentially lead to bugs and errors so a warning shouldn't be
 * ignored
 */
#define TOAST_WARN(sink, ...) TOAST_LOG_IMPL(::logging::Severity::warning, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief An error is a problem that should be addressed as soon as possible
 *
 * The engine will be considered as an undefined behaviour from the moment
 * an error happens since an "unrecoverable-in-runtime" problem has ocurred
 */
#define TOAST_ERROR(sink, ...) TOAST_LOG_IMPL(::logging::Severity::error, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Something has failed and we *cannot* recover from this
 *
 * We will terminate the process when a critical error happens via @c std::abort();
 */
#define TOAST_CRITICAL(sink, ...)                                     \
	do {                                                                \
		TOAST_LOG_IMPL(::logging::Severity::critical, sink, __VA_ARGS__); \
		std::abort();                                                     \
	} while (0)

/// critical in debug and an error otherwise
#ifdef DEBUG
#define TOAST_ASSERT_FAILED(sink, ...) TOAST_CRITICAL(sink, __VA_ARGS__)
#else
#define TOAST_ASSERT_FAILED(sink, ...) TOAST_ERROR(sink, __VA_ARGS__)
#endif

/**
 * @macro TOAST_ASSERT
 * @brief requires that a condition is met
 * @param condition evaluated in every build
 * @param sink class logging the message
 * @param ... fmt formatted message
 */
#define TOAST_ASSERT(condition, sink, ...)                                                          \
	do {                                                                                              \
		if (!(condition)) {                                                                             \
			TOAST_ASSERT_FAILED(sink, "Assertion failed ({}): {}", #condition, std::format(__VA_ARGS__)); \
		}                                                                                               \
	} while (0)

#define TOAST_NOT_IMPLEMENTED TOAST_ASSERT(false, "Engine", "Not implemented")
