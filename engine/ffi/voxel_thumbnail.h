/**
 * @file voxel_thumbnail.h
 * @author Xein
 * @date 24 Sep 2026
 * @brief Thumbnails for voxels
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

TOAST_C_API int
    toast_tvox_render_thumbnail(const char* tvox_path, const char* palette_path, uint8_t* dst, uint32_t thumb_size) NOEXCEPT;

#ifdef __cplusplus
}
#endif
