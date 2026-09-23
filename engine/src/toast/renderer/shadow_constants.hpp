/// @file shadow_constants.hpp
/// @author dario
/// @date 01/08/2026
///
/// @brief Mirrored by shadow_depth.slang and mesh.slang

#pragma once

#include <algorithm>
#include <cstdint>
#include <toast/export.hpp>

namespace renderer::shadows {

inline constexpr uint32_t k_cascade_count = 4;

inline constexpr uint32_t k_cascade_resolution = 1024;

inline constexpr float k_shadow_distance = 150.0f;

inline constexpr float k_cascade_split_lambda = 0.75f;

inline constexpr uint32_t k_max_spot_shadows = 4;

inline constexpr uint32_t k_max_point_shadows = 2;

inline constexpr uint32_t k_cube_faces = 6;

inline constexpr uint32_t k_punctual_resolution = 512;

inline constexpr uint32_t k_min_punctual_resolution = 64;

inline constexpr float k_punctual_full_resolution_distance = 10.0f;
inline constexpr float k_punctual_min_resolution_distance = 60.0f;

/// @note Set on the main thread before the passes are built
TOAST_API auto cascadeResolution() -> uint32_t;
TOAST_API auto punctualResolution() -> uint32_t;
TOAST_API auto shadowDistance() -> float;

TOAST_API void setCascadeResolution(uint32_t resolution);
TOAST_API void setPunctualResolution(uint32_t resolution);
TOAST_API void setShadowDistance(float distance);

[[nodiscard]]
inline auto punctualShadowResolution(float camera_distance, float resolution_scale) -> uint32_t {
	const uint32_t full_resolution = punctualResolution();
	const float span = k_punctual_min_resolution_distance - k_punctual_full_resolution_distance;
	float t = span > 0.0f ? (camera_distance - k_punctual_full_resolution_distance) / span : 1.0f;
	t = std::clamp(t, 0.0f, 1.0f);

	const auto max_resolution = static_cast<float>(full_resolution);
	const auto min_resolution = static_cast<float>(k_min_punctual_resolution);
	const float scale = std::clamp(resolution_scale, 0.0f, 1.0f);
	const float target = (max_resolution + ((min_resolution - max_resolution) * t)) * scale;

	uint32_t resolution = k_min_punctual_resolution;
	while ((resolution * 2) <= full_resolution && static_cast<float>(resolution * 2) <= target) {
		resolution *= 2;
	}
	return resolution;
}

/// Spots first then six faces per point light
inline constexpr uint32_t k_spot_layer_base = 0;
inline constexpr uint32_t k_point_layer_base = k_max_spot_shadows;
inline constexpr uint32_t k_punctual_layer_count = k_max_spot_shadows + (k_max_point_shadows * k_cube_faces);

/// Also sizes the shadow_depth.slang matrix array
inline constexpr uint32_t k_max_shadow_views = k_cascade_count + k_punctual_layer_count;

inline constexpr float k_punctual_near = 0.05f;

/// Must exceed the mesh.slang filter radius
inline constexpr float k_cube_face_guard_texels = 4.0f;

}
