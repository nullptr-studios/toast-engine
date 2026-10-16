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

TOAST_C_API void toast_create_SDL_window(const char*) NOEXCEPT;
TOAST_C_API void toast_create_avalonia_window() NOEXCEPT;

typedef struct {
	uint64_t uid;
	const char* name;
} workspace_result;

TOAST_C_API workspace_result toast_create_workspace(const char* type) NOEXCEPT;
TOAST_C_API workspace_result toast_open_workspace(const char* uid) NOEXCEPT;

/// @brief autosave recovery
TOAST_C_API workspace_result toast_open_workspace_from(const char* uid, const char* source_uri) NOEXCEPT;

TOAST_C_API workspace_result toast_play_workspace(uint64_t source_handle) NOEXCEPT;

/// @brief Renames the root node inside a .tnode or .tbnode file in-place
TOAST_C_API void toast_rename_prefab_root(const char* path, const char* new_name) NOEXCEPT;
/// @brief Creates a .tnode file with a properly-initialized node of the given type
TOAST_C_API void toast_create_tnode(const char* path, const char* node_type) NOEXCEPT;
/// @brief Clears unused cached assets and reloads the project manifest from disk
TOAST_C_API void toast_reload_manifest(void) NOEXCEPT;
/// @brief Plays a haptic described by .thaptic TOML text on the active controller
TOAST_C_API void toast_haptics_test(const char* toml_text) NOEXCEPT;

/**
 * @brief Selects the asset load mode (0 = editor text files, 1 = game binary packs)
 * @note Must be called before toast_init(); defaults to 0 (editor)
 */
TOAST_C_API void toast_set_load_mode(int mode) NOEXCEPT;

/**
 * @brief Mounts a .pak archive at a URI scheme
 * @param scheme URI scheme without "://" (e.g. "assets", "core")
 * @param pak_path Absolute path to the .pak file
 * @note Must be called before toast_init() so the manifest read uses the pack
 */
TOAST_C_API void toast_mount_pack(const char* scheme, const char* pak_path) NOEXCEPT;

/**
 * @brief Creates the World and loads the init_scene from project settings
 */
TOAST_C_API void toast_start_game(void) NOEXCEPT;

/**
 * @brief Calls begin() on the active application layer
 * @note Used by hot-reload
 */
TOAST_C_API void toast_begin_application(void) NOEXCEPT;

/**
 * @brief Destroys the active application layer and resets the pointer to null
 * @note Used by hot-reload
 */
TOAST_C_API void toast_pop_application(void) NOEXCEPT;

/**
 * @brief Serializes a node asset to binary format and writes it to out_path
 * @param uid 11-character UID string of the node asset
 * @param out_path Absolute filesystem path to write the binary .tnode to
 */
TOAST_C_API void toast_bake_asset(const char* uid, const char* out_path) NOEXCEPT;

/**
 * @brief Regenerates the schema's lua files
 * @param out_path Absolute path to the .d.lua file
 */
TOAST_C_API int toast_generate_data_schema_stubs(const char* out_path) NOEXCEPT;

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
/// @note While frames are shared as textures they are only read back after toast_viewport_request_readback()
TOAST_C_API int toast_viewport_get_frame(void* dst, uint32_t dst_capacity, toast_viewport_frame_t* out) NOEXCEPT;

/// @brief Reads the next frame back for toast_viewport_get_frame()
TOAST_C_API void toast_viewport_request_readback(void) NOEXCEPT;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint64_t frame_id;
	uint64_t generation;
	uint64_t memory_size;
	uint32_t slot;
	uint32_t slot_count;
} toast_viewport_shared_frame_t;

/// @brief Native handles of one shared slot
/// Windows: image is a D3D11 texture NT handle with a keyed mutex
/// Linux: image, ready and done are new opaque fds the caller owns
typedef struct {
	intptr_t image;
	intptr_t ready;
	intptr_t done;
} toast_viewport_shared_handles_t;

/// @brief Starts sharing viewport frames as textures instead of reading every one back
/// @param adapter_luid The compositor's adapter LUID (Windows)
/// @param adapter_uuid The compositor's device UUID (Linux)
TOAST_C_API int toast_viewport_enable_shared(uint64_t adapter_luid, const uint8_t* adapter_uuid) NOEXCEPT;

/// @brief Goes back to reading every frame back
TOAST_C_API void toast_viewport_disable_shared(void) NOEXCEPT;

TOAST_C_API int toast_viewport_get_shared_frame(toast_viewport_shared_frame_t* out) NOEXCEPT;

TOAST_C_API int
    toast_viewport_export_shared_slot(uint64_t generation, uint32_t slot, toast_viewport_shared_handles_t* out) NOEXCEPT;

/// @brief Keeps the renderer from writing @p slot while the compositor reads it
TOAST_C_API int toast_viewport_hold_shared_slot(uint64_t generation, uint32_t slot) NOEXCEPT;

/// @param consumed 1 when the compositor waited for ready and signalled done
TOAST_C_API void toast_viewport_release_shared_slot(uint64_t generation, uint32_t slot, int consumed) NOEXCEPT;

TOAST_C_API void toast_reload_project_settings(void) NOEXCEPT;

/// @return Maximum ticks per second from the project settings, 0 is uncapped
TOAST_C_API uint32_t toast_get_max_tick_rate(void) NOEXCEPT;

TOAST_C_API void toast_set_show_debug_windows(int visible) NOEXCEPT;

/// @brief Tells the renderer whether the host application is focused and whether it is minimized
/// @param focused 0 holds rendering at 30 fps (or the configured cap when lower), 1 restores the configured cap
/// @param minimized 1 stops rendering entirely until called again with 0
TOAST_C_API void toast_set_window_state(int focused, int minimized) NOEXCEPT;

#ifdef __cplusplus
}
#endif
