/**
 * @file trackpad.h
 * @author Xein
 * @date 16 Sep 2026
 * @brief Windows native touchpad gestures
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct toast_trackpad_state {
	float pan_x;
	float pan_y;
	float zoom;
	int32_t inverted;
} toast_trackpad_state;

TOAST_C_API int32_t toast_trackpad_supported(void) NOEXCEPT;
TOAST_C_API uint64_t toast_trackpad_create(void* native_window) NOEXCEPT;
TOAST_C_API void toast_trackpad_destroy(uint64_t handle) NOEXCEPT;
TOAST_C_API void toast_trackpad_set_rect(uint64_t handle, int32_t x, int32_t y, int32_t width, int32_t height) NOEXCEPT;
TOAST_C_API void toast_trackpad_update(uint64_t handle) NOEXCEPT;
TOAST_C_API int32_t toast_trackpad_drain(uint64_t handle, toast_trackpad_state* out_state) NOEXCEPT;
TOAST_C_API int32_t toast_trackpad_active(uint64_t handle) NOEXCEPT;

#ifdef __cplusplus
}
#endif
