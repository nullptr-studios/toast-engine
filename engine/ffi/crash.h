/// @file crash.h

#pragma once
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @note On in Debug builds. TOAST_CRASH_HANDLER=0 disables it and TOAST_CRASH_HANDLER=1 enables it in any build
TOAST_C_API void toast_crash_handler_install(void) NOEXCEPT;

/// @param kind 0 access violation. 1 vector subscript out of range. 2 uncaught C++ exception. 3 stack overflow. 4 abort()
TOAST_C_API void toast_crash_test(int kind) NOEXCEPT;

#ifdef __cplusplus
}
#endif
