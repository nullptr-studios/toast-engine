/// @file engine.h
/// @author Xein
/// @date 10 Feb 2026

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct engine_t;                                       ///< Opaque Engine ptr

TOAST_C_API engine_t* toast_create(void) NOEXCEPT;     ///< Creates the game engine
TOAST_C_API void toast_init(void) NOEXCEPT;            ///< Initializes the game engine after setting a working directory
TOAST_C_API void toast_tick(void) NOEXCEPT;            ///< Frame logic for the game engine
TOAST_C_API int toast_should_close(void) NOEXCEPT;     ///< @return 1 if the engine should close
TOAST_C_API void toast_destroy(engine_t*) NOEXCEPT;    ///< Destroys the game engine

/// mutually exclusive — call exactly one before toast_init(); SDL for standalone, Avalonia for editor
TOAST_C_API void toast_create_SDL_window(const char*) NOEXCEPT;
TOAST_C_API void toast_create_avalonia_window() NOEXCEPT;

typedef struct {
	uint64_t uid;
	const char* name;
} workspace_result;

TOAST_C_API workspace_result toast_create_workspace(const char* type) NOEXCEPT;
TOAST_C_API workspace_result toast_open_workspace(const char* uid) NOEXCEPT;

/// @brief Renames the root node inside a .tnode or .tbnode file in-place
TOAST_C_API void toast_rename_prefab_root(const char* path, const char* new_name) NOEXCEPT;
/// @brief Creates a .tnode file with a properly-initialized node of the given type
TOAST_C_API void toast_create_tnode(const char* path, const char* node_type) NOEXCEPT;
/// @brief Clears unused cached assets and reloads the project manifest from disk
TOAST_C_API void toast_reload_manifest(void) NOEXCEPT;
/// @brief Plays a haptic described by .thaptic TOML text on the active controller
TOAST_C_API void toast_haptics_test(const char* toml_text) NOEXCEPT;

// clang-format off
/// sets all five URI roots; must be called before toast_init()
/// @param assets  content addressed by UID (assets://)
/// @param artworks  raw art source files (artwork://)
/// @param cache   generated/baked files (cache://)
/// @param saved   user save data (saved://)
/// @param core    engine built-in assets (core://)
TOAST_C_API void toast_set_working_directory(const char* assets, const char* artworks, const char* cache, const char* saved, const char* core) NOEXCEPT;
// clang-format on

/// @brief Description of the latest rendered viewport frame
typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t row_pitch;    ///< Bytes per row (= width * 4 for BGRA8).
	uint64_t frame_id;     ///< Increments per published frame
} toast_viewport_frame_t;

/// @brief Copies the latest finished pixels into @p dst
/// @return 1 copied, 0 none available yet, -1 dst too small
TOAST_C_API int toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out) NOEXCEPT;

#ifdef __cplusplus
}
#endif
