/**
 * @file events.h
 * @author Xein
 * @date 15 Jun 2026
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*event_callback)(const char* name, const uint8_t* data, uint32_t size, void* user_data);

TOAST_C_API uint32_t events_listener_create(void);
TOAST_C_API void events_listener_destroy(uint32_t handle);
TOAST_C_API int
    events_listener_subscribe(uint32_t handle, const char* name, event_callback callback, void* user_data, char priority);
TOAST_C_API void events_listener_unsubscribe(uint32_t handle, const char* name);
TOAST_C_API int events_send(const char* name, const uint8_t* data, uint32_t size);
TOAST_C_API uint32_t events_count(void);
TOAST_C_API const char* events_name(uint32_t index);

#ifdef __cplusplus
}
#endif
