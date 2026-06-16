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
#include <string_view>

#ifdef DEBUG
#include <tracy/Tracy.hpp>
#endif

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
}

#define TOAST_FILE_NAME ::logging::_detail::getOnlyName(__FILE__)

#ifdef DEBUG
#define TOAST_LOG_IMPL(severity, sink, ...)                                  \
	do {                                                                       \
		std::string msg = std::format(__VA_ARGS__);                              \
		::logging::_detail::log(severity, TOAST_FILE_NAME, __LINE__, sink, msg); \
		constexpr uint32_t red = 0xFF0000;                                       \
		constexpr uint32_t green = 0x00FF00;                                     \
		constexpr uint32_t yellow = red | green; /* i love this so much -x */    \
		switch (severity) {                                                      \
			case 4:                                                                \
			case 3: TracyMessageC(msg.c_str(), msg.size(), red); break;            \
			case 2: TracyMessageC(msg.c_str(), msg.size(), yellow); break;         \
			case 1: TracyMessageC(msg.c_str(), msg.size(), green); break;          \
			default: break;                                                        \
		}                                                                        \
	} while (0)
#else
#define TOAST_LOG_IMPL(severity, sink, ...)                                                       \
	do {                                                                                            \
		::logging::_detail::log(severity, TOAST_FILE_NAME, __LINE__, sink, std::format(__VA_ARGS__)); \
	} while (0)
#endif

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
#define TOAST_TRACE(sink, ...) TOAST_LOG_IMPL(0, sink, __VA_ARGS__)

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
#define TOAST_INFO(sink, ...) TOAST_LOG_IMPL(1, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Indicates possible code missuse
 *
 * Something didn't happen as expected but should not be a major problem.
 * It can potentially lead to bugs and errors so a warning shouldn't be
 * ignored
 */
#define TOAST_WARN(sink, ...) TOAST_LOG_IMPL(2, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief An error is a problem that should be addressed as soon as possible
 *
 * The engine will be considered as an undefined behaviour from the moment
 * an error happens since an "unrecoverable-in-runtime" problem has ocurred
 */
#define TOAST_ERROR(sink, ...) TOAST_LOG_IMPL(3, sink, __VA_ARGS__)

/**
 * @param sink class logging the message
 * @param ... fmt formatted message
 * @brief Something has failed and we *cannot* recover from this
 *
 * We will terminate the process when a critical error happens via @c std::abort();
 */
#define TOAST_CRITICAL(sink, ...)         \
	do {                                    \
		TOAST_LOG_IMPL(4, sink, __VA_ARGS__); \
		std::abort();                         \
	} while (0)

#ifdef _DEBUG
/**
 * @macro TOAST_ASSERT
 * @brief requires that a condition is met, if not, the program will
 *        terminate with a critical failure
 * @param sink class logging the message
 * @param ... fmt formatted message
 */
#define TOAST_ASSERT(condition, sink, ...)  \
	do {                                      \
		if (!(condition)) {                     \
			TOAST_LOG_IMPL(4, sink, __VA_ARGS__); \
			assert(condition);                    \
		}                                       \
	} while (0)
#else
#define TOAST_ASSERT(condition, sink, ...)
#endif

#define TOAST_NOT_IMPLEMENTED TOAST_ASSERT(false, "Engine", "Not implemented")
