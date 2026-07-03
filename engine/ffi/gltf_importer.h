/**
 * @file gltf_importer.h
 * @author Xein
 * @date 12 Jun 2026
 * @brief Export functions for the GLTF importer to work on the editor
 */

#pragma once
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

TOAST_C_API void gltf_generate_intermediates(const char* path) NOEXCEPT;
TOAST_C_API void gltf_create_tnode(const char* json_path, const char* output_path) NOEXCEPT;

#ifdef __cplusplus
}
#endif
