/**
 * @file log.h
 * @author Xein
 * @date 19 Mar 2026
 * @brief C interface for the logging system
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
