/**
 * @file texture.h
 * @brief Export functions for decoding compressed textures for the editor (e.g. asset thumbnails)
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decodes (transcoding Basis Universal payloads if needed) a .ktx2 file and writes a nearest-neighbor
 * downsampled RGBA8 thumbnail into @p dst
 * @param path Absolute path to the .ktx2 file
 * @param dst Caller-owned buffer of exactly thumb_size * thumb_size * 4 bytes
 * @param thumb_size Width/height of the square thumbnail to produce
 * @return 1 on success, 0 on failure (file missing, not a valid KTX2, or an unsupported pixel format)
 */
TOAST_C_API int toast_ktx2_decode_thumbnail(const char* path, uint8_t* dst, uint32_t thumb_size) NOEXCEPT;

#ifdef __cplusplus
}
#endif
