/// @file gpu_types.hpp
/// @author dario
/// @date 30/10/2026

#pragma once

#include "toast/renderer/lighting/shadow_constants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace renderer {

constexpr uint32_t k_max_fog_volumes = 32;

/// Mirrors volumetrics.slang FogVolume
struct FogVolumeGpu {
	glm::mat4 world_to_local {1.0f};
	/// xyz half extents w edge fade in meters
	glm::vec4 extents_fade {0.0f};
	/// rgb albedo a extinction per meter
	glm::vec4 albedo_density {0.0f};
	/// rgb radiance per meter at density 1 a noise strength
	glm::vec4 emission_noise {0.0f};
	/// xyz wind in meters per second w noise scale in meters
	glm::vec4 wind_scale {0.0f};
	/// x anisotropy y ambient scale
	glm::vec4 phase {0.0f};
	/// xyz box centre in world space
	glm::vec4 centre {0.0f};
};

constexpr uint32_t k_max_directional_lights = 4;

constexpr uint32_t k_max_joint_matrices = 4096;

constexpr uint32_t k_max_instances = 16384;

constexpr uint32_t k_instance_no_motion = 1u << 0;

/// Mirrors mesh.slang depth_prepass.slang and shadow_depth.slang InstanceData
struct InstanceData {
	glm::mat4 model {1.0f};
	glm::mat4 previous_model {1.0f};
	uint32_t joint_offset = 0;
	uint32_t flags = 0;
	std::array<uint32_t, 2> pad0 {0, 0};
};

static_assert(sizeof(InstanceData) == 144, "InstanceData stride must match mesh.slang's std430 layout");

struct DirectionalLightData {
	/// w angular radius in radians when voxel shadows are marched and 0 for the shadow map
	glm::vec4 direction;
	glm::vec4 color_intensity;
};

constexpr uint32_t k_max_reflection_probes = 8;

struct ReflectionProbeData {
	glm::vec4 position_radius;
	// xyz box half extents w intensity. Zero extents use sphere projection
	glm::vec4 box_extents_intensity;

	// x cubemap index in registration order
	glm::vec4 params {0.0f};
};

constexpr uint32_t k_max_irradiance_volumes = 4;

constexpr uint32_t k_max_irradiance_probes = 4096;

constexpr uint32_t k_irradiance_capture_size = 64;

struct IrradianceVolumeData {
	/// xyz grid min corner w probe spacing
	glm::vec4 min_corner_spacing {0.0f};
	/// xyz probe counts w first probe index in the SH buffer
	glm::uvec4 counts_base {0};
	/// xyz half extents w intensity
	glm::vec4 extents_intensity {0.0f};

	/// x 1 once the SH slice of this volume is baked
	glm::uvec4 baked_pad {0};
};

struct FrameUBO {
	glm::mat4 view {1.0f};
	glm::mat4 projection {1.0f};
	glm::mat4 view_projection {1.0f};

	glm::vec3 camera_position {};

	float time = 0.0f;

	/// view_projection with the TAA jitter for world raster only
	glm::mat4 jittered_view_projection {1.0f};

	/// Unjittered view_projection of the last presented frame
	glm::mat4 previous_view_projection {1.0f};

	glm::vec4 ambient_color_intensity {0.0f};

	// uvec4 so std140 and cbuffer packing agree
	glm::uvec4 directional_light_count_pad {0};

	std::array<DirectionalLightData, k_max_directional_lights> directional_lights {};

	glm::uvec4 render_mode_pad {0};

	std::array<glm::mat4, shadows::k_cascade_count> cascade_view_projection {};

	glm::vec4 cascade_splits {0.0f};

	glm::vec4 cascade_texel_world_size {0.0f};

	glm::vec4 cascade_depth_bias {0.0f};

	// x caster index + 1 (0 none) y cascades fitted zw texel size in UV
	glm::vec4 shadow_params {0.0f};

	// x 1 when the cubemaps hold data y prefiltered roughness levels
	glm::vec4 environment_params {0.0f};

	glm::uvec4 reflection_probe_count_pad {0};

	std::array<ReflectionProbeData, k_max_reflection_probes> reflection_probes {};

	glm::uvec4 irradiance_volume_count_pad {0};

	std::array<IrradianceVolumeData, k_max_irradiance_volumes> irradiance_volumes {};

	// x 1 traces the TLAS instead of sampling shadow maps
	glm::vec4 traced_shadow_params {0.0f};

	/// xy blue noise texel offset walking R2 per frame z frame index
	glm::vec4 noise_params {0.0f};

	/// x height fog extinction per meter at the base height y base height z height falloff w fog volumes
	glm::vec4 fog_medium {0.0f};
	/// x height fog distance y fog volume distance z marched sun reach in meters
	glm::vec4 fog_reach {0.0f};
};

static_assert(
    offsetof(FrameUBO, jittered_view_projection) == 208,
    "FrameUBO::jittered_view_projection must match lighting.slang and depth_prepass.slang"
);
static_assert(
    offsetof(FrameUBO, irradiance_volumes) == 1264, "FrameUBO::irradiance_volumes must match lighting.slang's std140 layout"
);
static_assert(offsetof(FrameUBO, noise_params) == 1536, "FrameUBO::noise_params must match lighting.slang's std140 layout");
static_assert(
    offsetof(FrameUBO, traced_shadow_params) == 1520, "FrameUBO::traced_shadow_params must match lighting.slang's std140 layout"
);

/// Mirrors GpuLight in lighting.slang and cluster_lighting.slang
struct GpuLight {
	glm::vec4 world_pos_range;
	glm::vec4 view_pos_type;     // w type 0 point 1 spot
	glm::vec4 view_direction;    // xyz view space spot axis
	glm::vec4 color_intensity;
	glm::vec4 direction_pad;     // w shadow layer or -1
	glm::vec4 cone_angles;       // x cos outer y cos inner z shadow near w shadow far

	// x fraction of the layer rendered y point light face scale inside the guard band z shadow strength
	glm::vec4 shadow_atlas {1.0f, 1.0f, 0.0f, 0.0f};

	glm::mat4 shadow_view_projection {1.0f};

	/// x toast::LightShape y radius or rect half width z capsule half length or rect half height w toast::LightShadowMode
	/// The shape faces or runs along direction_pad.xyz
	glm::vec4 shape {0.0f};

	/// xyz rect width axis
	glm::vec4 shape_tangent {0.0f};
};

static_assert(sizeof(GpuLight) == 208, "GpuLight must match the stride in lighting.slang and cluster_lighting.slang");

}
