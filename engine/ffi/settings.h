/// @file settings.h
/// @author dario
/// @date 16/08/2026
///
///

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum toast_setting_type {
	TOAST_SETTING_BOOL = 0,
	TOAST_SETTING_INT = 1,
	TOAST_SETTING_FLOAT = 2,
	TOAST_SETTING_STRING = 3,
	TOAST_SETTING_VEC2 = 4,
	TOAST_SETTING_VEC3 = 5,
	TOAST_SETTING_VEC4 = 6,
	TOAST_SETTING_QUAT = 7,
};

enum toast_setting_kind {
	TOAST_SETTING_KIND_NORMAL = 0,
	TOAST_SETTING_KIND_ADVANCED = 1,
	TOAST_SETTING_KIND_DEBUG = 2,
	TOAST_SETTING_KIND_USER = 3,
};

/**
 * @brief Everything needed to draw one setting
 *
 * @note The string pointers reference storage the registry owns for the life of the process; settings are
 * never removed. They are only valid until the next call that could add one, which in practice means "read
 * them into managed strings as you enumerate"
 */
typedef struct {
	const char* key;
	const char* label;
	const char* category;
	const char* description;
	int32_t type;            ///< toast_setting_type
	double min;              ///< Equal min and max mean unbounded
	double max;
	double step;             ///< 0 lets the caller choose
	int32_t option_count;    ///< Non-zero makes an int setting a combo box; read with toast_settings_get_option
	int32_t requires_restart;
	int32_t hidden;
	int32_t kind;
	int32_t is_array;
	int32_t value_type;
} toast_setting_desc;

TOAST_C_API int32_t toast_settings_count(void) NOEXCEPT;

/// @return 1 when @p index named a setting and @p out was filled
TOAST_C_API int32_t toast_settings_get_desc(int32_t index, toast_setting_desc* out) NOEXCEPT;

/// @return the asset type a string setting picks, or an empty string for a plain one
TOAST_C_API const char* toast_settings_get_asset_type(int32_t index) NOEXCEPT;

TOAST_C_API const char* toast_settings_get_unit(int32_t index) NOEXCEPT;

/// @return option @p option of setting @p index, or NULL when either is out of range
TOAST_C_API const char* toast_settings_get_option(int32_t index, int32_t option) NOEXCEPT;

TOAST_C_API int32_t toast_settings_get_bool(const char* key) NOEXCEPT;
TOAST_C_API int64_t toast_settings_get_int(const char* key) NOEXCEPT;
TOAST_C_API double toast_settings_get_float(const char* key) NOEXCEPT;

/// @return the value in a buffer valid until this thread's next call to it
TOAST_C_API const char* toast_settings_get_string(const char* key) NOEXCEPT;

TOAST_C_API void toast_settings_set_bool(const char* key, int32_t value) NOEXCEPT;
TOAST_C_API void toast_settings_set_int(const char* key, int64_t value) NOEXCEPT;
TOAST_C_API void toast_settings_set_float(const char* key, double value) NOEXCEPT;
TOAST_C_API void toast_settings_set_string(const char* key, const char* value) NOEXCEPT;

TOAST_C_API void toast_settings_reset(const char* key) NOEXCEPT;
TOAST_C_API void toast_settings_reset_all(void) NOEXCEPT;

/// @return 1 on success
TOAST_C_API int32_t toast_settings_save(void) NOEXCEPT;
TOAST_C_API int32_t toast_settings_is_dirty(void) NOEXCEPT;

TOAST_C_API const char* toast_settings_active_path(void) NOEXCEPT;

TOAST_C_API const char* toast_settings_get_text(const char* key) NOEXCEPT;

TOAST_C_API const char* toast_settings_get_default_text(const char* key) NOEXCEPT;

TOAST_C_API int32_t toast_settings_set_text(const char* key, const char* text) NOEXCEPT;

TOAST_C_API const char* toast_settings_last_error(void) NOEXCEPT;

TOAST_C_API int32_t toast_settings_is_overridden(const char* key) NOEXCEPT;

TOAST_C_API void toast_settings_restore(void) NOEXCEPT;

TOAST_C_API int32_t toast_settings_cook(const char* config_dir, const char* toast_file, const char* stage_dir) NOEXCEPT;

#ifdef __cplusplus
}
#endif
