/**
 * @file audio.h
 * @author Xein
 * @date 29 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

TOAST_C_API void audio_generate_intermediates(const char* path) NOEXCEPT;

#ifdef __cplusplus
}
#endif
