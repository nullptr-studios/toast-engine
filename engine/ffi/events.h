/**
 * @file events.h
 * @author Xein
 * @date 15 Jun 2026
 *
 * @brief C API for the event system; used by the C# editor/player via P/Invoke
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// callback fired when an event arrives from the engine; name is the protobuf message name,
/// data/size are the serialized bytes, user_data is the C# GCHandle pinned on the managed side
typedef int (*event_callback)(const char* name, const uint8_t* data, uint32_t size, void* user_data) NOEXCEPT;

TOAST_C_API uint32_t events_listener_create(void) NOEXCEPT;    ///< allocates a native Listener and returns an opaque handle
TOAST_C_API void events_listener_destroy(uint32_t handle) NOEXCEPT;    ///< destroys the Listener and unsubscribes all callbacks
TOAST_C_API int events_listener_subscribe(
    uint32_t handle, const char* name, event_callback callback, void* user_data, char priority
) NOEXCEPT;                                                            ///< @return 0 on success, -1 if the event type is unknown
TOAST_C_API void events_listener_unsubscribe(
    uint32_t handle, const char* name
) NOEXCEPT;    ///< removes all callbacks with this label from the listener
TOAST_C_API int events_send(
    const char* name, const uint8_t* data, uint32_t size
) NOEXCEPT;    ///< deserializes and enqueues an event from C#; @return 0 ok, -1 unknown type, -2 parse error
TOAST_C_API uint32_t events_count(void) NOEXCEPT;    ///< number of registered exposed event types; used by C# to enumerate them
TOAST_C_API const char* events_name(uint32_t index) NOEXCEPT;    ///< protobuf message name for the nth exposed event type

#ifdef __cplusplus
}
#endif
