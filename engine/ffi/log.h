/**
 * @file log.h
 * @author Xein
 * @date 19 Mar 2026
 * @brief C interface for the logging system
 *
 * Because of implementation reasons, sinks are passed by name rather than with a type
 *
 * In C++ we can safely test if the thing we're passing is a type or not, however, I do
 * not really know if you can do such thing in C, and, most important, if it could work
 * across the ABI, so we just hope the client (dario) doesn't write any typo when logging
 * through rust or C#
 *
 * Actually, not that this is a problem because dario would shoot himself before coding a
 * module in rust
 */

#pragma once
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

TOAST_C_API void toast_trace(const char* sink, const char* message, const char* file_name, unsigned line_number);
TOAST_C_API void toast_info(const char* sink, const char* message, const char* file_name, unsigned line_number);
TOAST_C_API void toast_warn(const char* sink, const char* message, const char* file_name, unsigned line_number);
TOAST_C_API void toast_error(const char* sink, const char* message, const char* file_name, unsigned line_number);
TOAST_C_API void toast_critical(const char* sink, const char* message, const char* file_name, unsigned line_number);

#ifdef __cplusplus
}
#endif

