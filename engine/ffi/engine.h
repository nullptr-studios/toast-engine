/// @file engine.h
/// @author Xein
/// @date 10 Feb 2026

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct engine_t;                              ///< Opaque Engine ptr

TOAST_C_API engine_t* toast_create(void);     ///< Creates the game engine
TOAST_C_API void toast_init(void);            ///< Initializes the game engine after setting a working directory
TOAST_C_API void toast_tick(void);            ///< Frame logic for the game engine
TOAST_C_API int toast_should_close(void);     ///< @return 1 if the engine should close
TOAST_C_API void toast_destroy(engine_t*);    ///< Destroys the game engine

TOAST_C_API void toast_create_SDL_window(const char*);
TOAST_C_API void toast_create_avalonia_window();

typedef struct {
	uint64_t uid;
	const char* name;
} workspace_result;

TOAST_C_API workspace_result toast_create_workspace(const char* type);
TOAST_C_API workspace_result toast_open_workspace(const char* uid);

TOAST_C_API void
    toast_set_working_directory(const char* assets, const char* artworks, const char* cache, const char* saved, const char* core);

/// @brief Description of the latest rendered viewport frame
typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t row_pitch;    ///< Bytes per row (= width * 4 for BGRA8).
	uint64_t frame_id;     ///< Increments per published frame
} toast_viewport_frame_t;

/// @brief Copies the latest finished pixels into @p dst
/// @return 1 copied, 0 none available yet, -1 dst too small
TOAST_C_API int toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out);

#ifdef __cplusplus
}
#endif
