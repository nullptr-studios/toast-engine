/// @file renderer.h
/// @author dario
/// @date 16/08/2026
///
/// @brief Renderer commands and status for the editor's settings window
///
/// Everything tweakable lives in settings.h instead

#pragma once
#include "export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Queues a capture of every registered reflection probe
 *
 * Six frames per probe, one cube face each, rendered through the viewport - so the viewport visibly runs the
 * bake rather than it happening in the background
 */
TOAST_C_API void toast_renderer_bake_reflection_probes(void) NOEXCEPT;

/// @brief Queues a bake of every irradiance volume; six frames per probe, and a volume holds many probes
TOAST_C_API void toast_renderer_bake_irradiance_volumes(void) NOEXCEPT;

/// @return probes never baked, or moved/resized since they were - stale data reads as wrong lighting rather
///         than as missing data, which is why this is surfaced rather than left to be noticed
TOAST_C_API uint32_t toast_renderer_stale_reflection_probes(void) NOEXCEPT;

TOAST_C_API uint32_t toast_renderer_stale_irradiance_volumes(void) NOEXCEPT;

/// @return total irradiance probes across every registered volume, which is what sets a bake's length
TOAST_C_API uint32_t toast_renderer_irradiance_probe_count(void) NOEXCEPT;

/// @return 1 when the device supports ray query, so the traced-shadow setting can do anything at all
TOAST_C_API int32_t toast_renderer_supports_ray_tracing(void) NOEXCEPT;

#ifdef __cplusplus
}
#endif
