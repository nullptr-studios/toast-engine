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

/// Cascade depth past its center in radii
inline constexpr float k_cascade_receiver_depth_radii = 4.0f;

/// Ceilings of the slot settings which size the shader matrix arrays
inline constexpr uint32_t k_max_spot_shadows = 32;
inline constexpr uint32_t k_max_point_shadows = 32;

inline constexpr uint32_t k_default_spot_shadows = 8;
inline constexpr uint32_t k_default_point_shadows = 6;

inline constexpr uint32_t k_cube_faces = 6;

inline constexpr uint32_t k_punctual_resolution = 512;

inline constexpr uint32_t k_min_punctual_resolution = 64;

/// Camera distance in light ranges within which a light keeps its full tile and past which each doubling halves it
inline constexpr float k_punctual_full_resolution_ranges = 4.0f;

/// Share of the slice radius a cascade covers beyond its slice so small camera moves reuse its cached map
inline constexpr float k_cascade_padding = 0.15f;

/// @note Set on the main thread before the passes are built
TOAST_API auto cascadeResolution() -> uint32_t;
TOAST_API auto punctualResolution() -> uint32_t;
TOAST_API auto shadowDistance() -> float;

TOAST_API void setCascadeResolution(uint32_t resolution);
TOAST_API void setPunctualResolution(uint32_t resolution);
TOAST_API void setShadowDistance(float distance);

/// Allocated slots with one punctual layer per spot and six per point
/// @note Set on the main thread before the passes are built
TOAST_API auto spotShadowSlots() -> uint32_t;
TOAST_API auto pointShadowSlots() -> uint32_t;
TOAST_API void setSpotShadowSlots(uint32_t slots);
TOAST_API void setPointShadowSlots(uint32_t slots);

/// Runtime tuning read every frame and set from the renderer settings
/// Shadow slots in use where the rest of the allocated slots stay empty
TOAST_API auto spotShadowBudget() -> uint32_t;
TOAST_API auto pointShadowBudget() -> uint32_t;
TOAST_API auto punctualFullResolutionRanges() -> float;
TOAST_API auto cascadeSplitLambda() -> float;
TOAST_API auto cascadePadding() -> float;
TOAST_API auto cascadeReceiverDepthRadii() -> float;
/// Resting voxel casters render once into a static map that each frame copies under the moving casters
TOAST_API auto staticCasterCache() -> bool;
/// Voxel casters rasterize task and mesh shader quads where the device has them instead of marching per texel
TOAST_API auto voxelMeshShadows() -> bool;
TOAST_API auto casterTolerance() -> float;

TOAST_API void setSpotShadowBudget(uint32_t lights);
TOAST_API void setPointShadowBudget(uint32_t lights);
TOAST_API void setPunctualFullResolutionRanges(float ranges);
TOAST_API void setCascadeSplitLambda(float lambda);
TOAST_API void setCascadePadding(float padding);
TOAST_API void setCascadeReceiverDepthRadii(float radii);
TOAST_API void setStaticCasterCache(bool enabled);
TOAST_API void setCasterTolerance(float meters);
TOAST_API void setVoxelMeshShadows(bool enabled);

/// Follows how large the lit sphere looks from the camera so a wide light far away keeps its detail
[[nodiscard]]
inline auto punctualShadowResolution(float camera_distance, float light_range, float resolution_scale) -> uint32_t {
	const uint32_t full_resolution = punctualResolution();
	const float reach = punctualFullResolutionRanges() * std::max(light_range, 0.0f);
	const float coverage = camera_distance > reach ? reach / camera_distance : 1.0f;

	const float scale = std::clamp(resolution_scale, 0.0f, 1.0f);
	const float target = static_cast<float>(full_resolution) * coverage * scale;

	uint32_t resolution = k_min_punctual_resolution;
	while ((resolution * 2) <= full_resolution && static_cast<float>(resolution * 2) <= target) {
		resolution *= 2;
	}
	return resolution;
}

/// Spots first then six faces per point light
inline constexpr uint32_t k_spot_layer_base = 0;

[[nodiscard]]
inline auto pointLayerBase() -> uint32_t {
	return spotShadowSlots();
}

/// At least one so the punctual map exists with every slot turned off
[[nodiscard]]
inline auto punctualLayerCount() -> uint32_t {
	return std::max(spotShadowSlots() + (pointShadowSlots() * k_cube_faces), 1u);
}

/// Mirrored by kMaxShadowViews in shadow_depth.slang and shadow_views.slang
inline constexpr uint32_t k_max_shadow_views = k_cascade_count + k_max_spot_shadows + (k_max_point_shadows * k_cube_faces);

/// Within kPunctualBiasMeters of lighting.slang so a stale pose never shadows itself
inline constexpr float k_caster_tolerance = 0.01f;
inline constexpr float k_punctual_near = 0.05f;

/// Must exceed the mesh.slang filter radius
inline constexpr float k_cube_face_guard_texels = 4.0f;

}
