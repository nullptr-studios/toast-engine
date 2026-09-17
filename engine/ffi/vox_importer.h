/**
 * @file vox_importer.h
 * @author dario
 * @date 12 Sep 2026
 */

#pragma once
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @param palette_uid 11 character UID or empty for none
TOAST_C_API void vox_generate_intermediates(const char* source_path, const char* out_dir, const char* palette_uid) NOEXCEPT;

TOAST_C_API void vox_create_tnode(const char* manifest_path, const char* output_path) NOEXCEPT;

#ifdef __cplusplus
}
#endif
