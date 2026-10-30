/// @file vulkan_renderer_lights.cpp
/// @author dario
/// @date 16/05/2026

#include "toast/renderer/core/vulkan_debug.hpp"
#include "toast/renderer/debug/editor_overlays.hpp"
#include "toast/renderer/debug/voxel_debug.hpp"
#include "toast/renderer/lighting/clustered_lighting_constants.hpp"
#include "toast/renderer/lighting/cube_face_basis.hpp"
#include "toast/renderer/lighting/frustum.hpp"
#include "toast/renderer/lighting/light_culling.hpp"
#include "toast/renderer/lighting/shadow_slots.hpp"
#include "toast/renderer/passes/debug/selection_outline.hpp"
#include "toast/renderer/passes/geometry/debris_pass.hpp"
#include "toast/renderer/passes/geometry/depth_prepass.hpp"
#include "toast/renderer/passes/geometry/material_pass.hpp"
#include "toast/renderer/passes/geometry/skinning_pass.hpp"
#include "toast/renderer/passes/geometry/voxel_pass.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/passes/lighting/reflection_probe_pass.hpp"
#include "toast/renderer/passes/lighting/shadow_pass.hpp"
#include "toast/renderer/passes/lighting/traced_specular_pass.hpp"
#include "toast/renderer/passes/post/taa_pass.hpp"
#include "toast/renderer/raytracing/ray_tracing_scene.hpp"
#include "toast/renderer/raytracing/skinned_blas_pool.hpp"
#include "toast/renderer/resources/blue_noise.hpp"
#include "toast/renderer/settings/renderer_settings.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/vulkan_renderer.hpp"
#include "toast/renderer/vulkan_renderer_detail.hpp"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/material.hpp>
#include <toast/log.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/profiler_phases.hpp>
#include <toast/settings/settings.hpp>
#include <toast/thread_pool.hpp>
#include <toast/time.hpp>
#include <toast/voxel/nodes/voxel_node.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/volume_bounds.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/window/window_events.hpp>
#include <toast/world/animation_player.hpp>
#include <toast/world/camera.hpp>
#include <toast/world/directional_light.hpp>
#include <toast/world/fog_volume.hpp>
#include <toast/world/irradiance_volume.hpp>
#include <toast/world/light.hpp>
#include <toast/world/light_gizmo.hpp>
#include <toast/world/mesh_node.hpp>
#include <toast/world/point_light.hpp>
#include <toast/world/post_process_volume.hpp>
#include <toast/world/reflection_probe.hpp>
#include <toast/world/spotlight.hpp>
#include <toast/world/workspace_events.hpp>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <unordered_set>

#if defined(_WIN32) && defined(TOAST_DEV_TOOLS)
#include <external/inc/nsight/NGFX_GPUTrace_Vulkan.h>
#include <external/inc/nsight/NGFX_GraphicsCapture_Vulkan.h>
#endif

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace renderer {

namespace {

struct DirectionalCascadeFit {
	glm::mat4 view_projection {1.0f};
	glm::vec4 cull_sphere {0.0f};
	float depth_bias = 0.0f;
	float texel_world_size = 0.0f;
};

constexpr float k_caster_back_off = 100.0f;

struct BoundingSphere {
	glm::vec3 center {0.0f};
	float radius = 0.0f;
};

auto frustumSliceSphere(const toast::Camera& camera, float aspect, float split_near, float split_far) -> BoundingSphere {
	const float tan_half_fov = std::tan(glm::radians(camera.fov) * 0.5f);
	const glm::mat4 inverse_view = glm::inverse(camera.getView());

	std::array<glm::vec3, 8> corners {};
	size_t corner_index = 0;
	for (const float split : {split_near, split_far}) {
		const float half_height = split * tan_half_fov;
		const float half_width = half_height * aspect;
		for (const float sx : {-1.0f, 1.0f}) {
			for (const float sy : {-1.0f, 1.0f}) {
				corners[corner_index++] = glm::vec3(inverse_view * glm::vec4(sx * half_width, sy * half_height, -split, 1.0f));
			}
		}
	}

	glm::vec3 center(0.0f);
	for (const auto& corner : corners) {
		center += corner;
	}
	center /= static_cast<float>(corners.size());

	float radius = 0.0f;
	for (const auto& corner : corners) {
		radius = std::max(radius, glm::distance(center, corner));
	}
	return {.center = center, .radius = radius};
}

auto fitDirectionalCascade(glm::vec3 center, float radius, glm::vec3 light_direction, uint32_t resolution)

    -> DirectionalCascadeFit {
	constexpr float k_world_depth_bias = 0.01f;

	radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 0.001f);

	const glm::vec3 direction = glm::normalize(light_direction);
	const glm::vec3 up =
	    std::abs(glm::dot(direction, toast::Node3D::world_up)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : toast::Node3D::world_up;

	const glm::mat4 light_view = glm::lookAt(center - direction * (radius + k_caster_back_off), center, up);

	const float depth_extent = (radius * (1.0f + shadows::cascadeReceiverDepthRadii())) + k_caster_back_off;

	// orthoRH_ZO since GLM_FORCE_DEPTH_ZERO_TO_ONE is not defined
	glm::mat4 light_projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, depth_extent);

	const float half_resolution = static_cast<float>(resolution) * 0.5f;
	glm::vec4 shadow_origin = light_projection * light_view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	shadow_origin *= half_resolution;
	glm::vec4 snap_offset = (glm::round(shadow_origin) - shadow_origin) / half_resolution;
	snap_offset.z = 0.0f;
	snap_offset.w = 0.0f;
	light_projection[3] += snap_offset;

	return DirectionalCascadeFit {
	  .view_projection = light_projection * light_view,
	  .cull_sphere = glm::vec4(center, radius),
	  .depth_bias = k_world_depth_bias / depth_extent,
	  .texel_world_size = 2.0f * radius / static_cast<float>(resolution),
	};
}

/// Fades out over the slack past the shadow distance before the slot is released
auto punctualShadowStrength(float camera_distance, float shadow_distance) -> float {
	if (shadow_distance <= 0.0f) {
		return 1.0f;
	}
	const float fade_end = shadow_distance * shadow_slots::k_distance_slack;
	return std::clamp((fade_end - camera_distance) / (fade_end - shadow_distance), 0.0f, 1.0f);
}

auto computeCascadeSplits(float near_plane, float far_plane) -> std::array<float, shadows::k_cascade_count> {
	std::array<float, shadows::k_cascade_count> splits {};
	const float range = far_plane - near_plane;
	const float lambda = shadows::cascadeSplitLambda();

	for (uint32_t i = 0; i < shadows::k_cascade_count; ++i) {
		const float fraction = static_cast<float>(i + 1) / static_cast<float>(shadows::k_cascade_count);
		const float logarithmic = near_plane * std::pow(far_plane / near_plane, fraction);
		const float uniform = near_plane + (range * fraction);
		splits[i] = glm::mix(uniform, logarithmic, lambda);
	}

	return splits;
}

/// @warning Not interchangeable with cubeFaceBasis()
auto shadowCubeFaceBasis(uint32_t face) -> std::pair<glm::vec3, glm::vec3> {
	switch (face) {
		case 0:
			return {
			  {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
			};
		case 1:
			return {
			  {-1.0f, 0.0f, 0.0f},
        { 0.0f, 0.0f, 1.0f}
			};
		case 2:
			return {
			  {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
			};
		case 3:
			return {
			  {0.0f, -1.0f, 0.0f},
        {0.0f,  0.0f, 1.0f}
			};
		case 4:
			return {
			  {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f}
			};
		default:
			return {
			  {0.0f, 0.0f, -1.0f},
        {0.0f, 1.0f,  0.0f}
			};
	}
}

}

void VulkanRenderer::assignShadowSlots(
    const std::vector<PunctualShadowCandidate>& candidates, std::span<shadow_slots::Slot> slots
) {
	m_tick_slot_candidates.clear();
	for (const PunctualShadowCandidate& candidate : candidates) {
		m_tick_slot_candidates.push_back({.key = candidate.key, .importance = candidate.importance, .visible = candidate.visible});
	}
	m_tick_slot_of.resize(candidates.size());
	m_shadow_displaced_total += shadow_slots::assign(m_tick_slot_candidates, slots, m_tick_slot_of);
}

void VulkanRenderer::releaseShadowSlots() {
	std::ranges::fill(m_spot_slots, shadow_slots::Slot {});
	std::ranges::fill(m_point_slots, shadow_slots::Slot {});
}

void VulkanRenderer::fitShadowViews(
    RenderFrame& frame, float aspect, int32_t shadow_caster_index, const glm::vec3& shadow_caster_direction,
    const std::vector<PunctualShadowCandidate>& spot_candidates, const std::vector<PunctualShadowCandidate>& point_candidates
) {
	ZoneScoped;
	if (m_shadow_pass == nullptr) {
		releaseShadowSlots();
		return;
	}
	frame.frame_data.shadow_params.z = 1.0f / static_cast<float>(shadows::cascadeResolution());
	frame.frame_data.shadow_params.w = 1.0f / static_cast<float>(shadows::punctualResolution());

	if (shadow_caster_index >= 0) {
		const float shadow_far = std::min(m_camera->far_plane, shadows::shadowDistance());
		const auto splits = computeCascadeSplits(m_camera->near_plane, shadow_far);

		const glm::vec3 light_direction = glm::normalize(shadow_caster_direction);
		const uint32_t resolution = shadows::cascadeResolution();
		const float padding = shadows::cascadePadding();
		float split_near = m_camera->near_plane;
		uint32_t cascades_refit = 0;
		for (uint32_t cascade = 0; cascade < shadows::k_cascade_count; ++cascade) {
			const BoundingSphere slice = frustumSliceSphere(*m_camera, aspect, split_near, splits[cascade]);

			// A zoom that leaves the padding far too large refits too
			CascadeAnchor& anchor = m_cascade_anchors[cascade];
			const bool holds = anchor.resolution == resolution && glm::dot(anchor.light_direction, light_direction) > 0.99999f &&
			                   glm::distance(anchor.center, slice.center) + slice.radius <= anchor.radius &&
			                   slice.radius * (1.0f + (2.0f * padding)) >= anchor.radius;
			if (!holds) {
				++cascades_refit;
				anchor = CascadeAnchor {
				  .center = slice.center,
				  .radius = slice.radius * (1.0f + padding),
				  .light_direction = light_direction,
				  .resolution = resolution,
				};
			}
			const auto fit = fitDirectionalCascade(anchor.center, anchor.radius, light_direction, resolution);

			frame.frame_data.cascade_view_projection[cascade] = fit.view_projection;
			frame.frame_data.cascade_splits[static_cast<int>(cascade)] = splits[cascade];
			frame.frame_data.cascade_depth_bias[static_cast<int>(cascade)] = fit.depth_bias;
			frame.frame_data.cascade_texel_world_size[static_cast<int>(cascade)] = fit.texel_world_size;

			frame.shadows.matrices.push_back(fit.view_projection);
			frame.shadows.views.push_back(
			    ShadowView {
			      .view_projection = fit.view_projection,
			      .cull_sphere = fit.cull_sphere,
			      .layer = cascade,
			      .directional = true,
			      .resolution = shadows::cascadeResolution(),
			    }
			);

			split_near = splits[cascade];
		}
		TracyPlot("Shadow: cascades refit", static_cast<int64_t>(cascades_refit));

		frame.frame_data.shadow_params.x = static_cast<float>(shadow_caster_index + 1);
		frame.frame_data.shadow_params.y = static_cast<float>(shadows::k_cascade_count);
	}

	// Slots past a lowered budget are released so their lights stop casting at once
	const auto budget_slots = [](std::span<shadow_slots::Slot> slots, uint32_t budget) {
		const size_t used = std::min<size_t>(budget, slots.size());
		std::ranges::fill(slots.subspan(used), shadow_slots::Slot {});
		return slots.first(used);
	};
	// Only lights reaching the view
	const auto denied = [this](const std::vector<PunctualShadowCandidate>& candidates) {
		uint32_t count = 0;
		for (size_t i = 0; i < candidates.size(); ++i) {
			count += m_tick_slot_of[i] < 0 && candidates[i].visible ? 1u : 0u;
		}
		return count;
	};

	m_spot_slots.resize(shadows::spotShadowSlots());
	m_point_slots.resize(shadows::pointShadowSlots());
	assignShadowSlots(spot_candidates, budget_slots(m_spot_slots, shadows::spotShadowBudget()));
	frame.light_stats.spot_denied = denied(spot_candidates);
	for (size_t i = 0; i < spot_candidates.size(); ++i) {
		const int32_t slot = m_tick_slot_of[i];
		if (slot < 0) {
			continue;
		}
		const PunctualShadowCandidate& candidate = spot_candidates[i];
		GpuLight& light = frame.lights[candidate.light_index];

		const glm::vec3 position = glm::vec3(light.world_pos_range);
		const glm::vec3 direction = glm::normalize(glm::vec3(light.direction_pad));
		const float range = std::max(light.world_pos_range.w, 0.01f);

		constexpr float k_cone_fov_padding = 1.1f;
		const float outer_cos = std::clamp(light.cone_angles.x, -1.0f, 1.0f);
		const float fov = std::min(2.0f * std::acos(outer_cos) * k_cone_fov_padding, glm::radians(179.0f));

		const glm::vec3 up =
		    std::abs(glm::dot(direction, toast::Node3D::world_up)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : toast::Node3D::world_up;
		const glm::mat4 view = glm::lookAt(position, position + direction, up);
		const glm::mat4 projection = glm::perspectiveRH_ZO(fov, 1.0f, shadows::k_punctual_near, range);
		const glm::mat4 view_projection = projection * view;

		auto& owner = m_spot_slots[static_cast<size_t>(slot)];
		owner.resolution = shadow_slots::stableResolution(
		    owner.resolution, std::sqrt(candidate.distance_squared), range, candidate.resolution_scale
		);
		const uint32_t resolution = owner.resolution;

		const uint32_t layer = shadows::k_spot_layer_base + static_cast<uint32_t>(slot);
		light.direction_pad.w = static_cast<float>(layer);
		light.cone_angles.z = shadows::k_punctual_near;
		light.cone_angles.w = range;
		light.shadow_atlas.x = static_cast<float>(resolution) / static_cast<float>(shadows::punctualResolution());
		light.shadow_atlas.z = punctualShadowStrength(std::sqrt(candidate.distance_squared), candidate.shadow_distance);
		light.shadow_view_projection = view_projection;

		frame.shadows.views.push_back(
		    ShadowView {
		      .view_projection = view_projection,
		      .cull_sphere = glm::vec4(position, range),
		      .layer = layer,
		      .directional = false,
		      .resolution = resolution,
		    }
		);
		frame.shadows.matrices.push_back(view_projection);
	}

	assignShadowSlots(point_candidates, budget_slots(m_point_slots, shadows::pointShadowBudget()));
	frame.light_stats.point_denied = denied(point_candidates);
	for (size_t i = 0; i < point_candidates.size(); ++i) {
		const int32_t slot = m_tick_slot_of[i];
		if (slot < 0) {
			continue;
		}
		const PunctualShadowCandidate& candidate = point_candidates[i];
		GpuLight& light = frame.lights[candidate.light_index];

		const glm::vec3 position = glm::vec3(light.world_pos_range);
		const float range = std::max(light.world_pos_range.w, 0.01f);

		auto& owner = m_point_slots[static_cast<size_t>(slot)];
		owner.resolution = shadow_slots::stableResolution(
		    owner.resolution, std::sqrt(candidate.distance_squared), range, candidate.resolution_scale
		);
		const uint32_t resolution = owner.resolution;

		const uint32_t base_layer = shadows::pointLayerBase() + (static_cast<uint32_t>(slot) * shadows::k_cube_faces);
		light.direction_pad.w = static_cast<float>(base_layer);
		light.cone_angles.z = shadows::k_punctual_near;
		light.cone_angles.w = range;
		light.shadow_atlas.x = static_cast<float>(resolution) / static_cast<float>(shadows::punctualResolution());
		light.shadow_atlas.z = punctualShadowStrength(std::sqrt(candidate.distance_squared), candidate.shadow_distance);

		const float guard_ndc_scale =
		    static_cast<float>(resolution) / (static_cast<float>(resolution) + (2.0f * shadows::k_cube_face_guard_texels));
		light.shadow_atlas.y = guard_ndc_scale;

		const float face_fov = 2.0f * std::atan(1.0f / guard_ndc_scale);

		for (uint32_t face = 0; face < shadows::k_cube_faces; ++face) {
			const auto [face_direction, face_up] = shadowCubeFaceBasis(face);
			const glm::mat4 view = glm::lookAt(position, position + face_direction, face_up);
			const glm::mat4 projection = glm::perspectiveRH_ZO(face_fov, 1.0f, shadows::k_punctual_near, range);
			const glm::mat4 view_projection = projection * view;

			frame.shadows.views.push_back(
			    ShadowView {
			      .view_projection = view_projection,
			      .cull_sphere = glm::vec4(position, range),
			      .layer = base_layer + face,
			      .directional = false,
			      .resolution = resolution,
			    }
			);
			frame.shadows.matrices.push_back(view_projection);
		}
	}
}

void VulkanRenderer::finalizeLights(RenderFrame& frame) {
	ZoneScoped;

	auto& lights = frame.lights;
	auto& meta = m_tick_light_meta;

	size_t kept = 0;
	for (size_t i = 0; i < lights.size(); ++i) {
		const bool owns_shadow = lights[i].direction_pad.w >= 0.0f;
		if (!meta[i].visible && !owns_shadow) {
			++frame.light_stats.offscreen;
			continue;
		}
		if (owns_shadow) {
			meta[i].importance = std::numeric_limits<float>::max();
		}
		lights[kept] = lights[i];
		meta[kept] = meta[i];
		++kept;
	}
	lights.resize(kept);
	meta.resize(kept);

	float light_reach = 0.0f;
	if (kept > clustered_lighting::k_max_lights_per_cluster) {
		auto& order = m_tick_light_order;
		order.resize(kept);
		std::iota(order.begin(), order.end(), 0U);
		std::ranges::stable_sort(order, [&meta](uint32_t a, uint32_t b) { return meta[a].importance > meta[b].importance; });

		const size_t final_count = std::min<size_t>(kept, clustered_lighting::k_max_lights);
		frame.light_stats.truncated = static_cast<uint32_t>(kept - final_count);

		auto& scratch = m_tick_light_scratch;
		scratch.assign(lights.begin(), lights.end());
		lights.resize(final_count);
		for (size_t i = 0; i < final_count; ++i) {
			lights[i] = scratch[order[i]];
			if (meta[order[i]].visible) {
				light_reach = std::max(light_reach, meta[order[i]].depth_reach);
			}
		}
	} else {
		for (const LightMeta& entry : meta) {
			if (entry.visible) {
				light_reach = std::max(light_reach, entry.depth_reach);
			}
		}
	}

	const auto occupied = [](const auto& slots) {
		return static_cast<uint32_t>(std::ranges::count_if(slots, [](const shadow_slots::Slot& slot) { return slot.key != 0; }));
	};
	frame.light_stats.spot_shadows = occupied(m_spot_slots);
	frame.light_stats.point_shadows = occupied(m_point_slots);
	frame.light_stats.shadow_displaced = m_shadow_displaced_total;
	TracyPlot("Shadow: spot slots used", static_cast<int64_t>(frame.light_stats.spot_shadows));
	TracyPlot("Shadow: point slots used", static_cast<int64_t>(frame.light_stats.point_shadows));
	TracyPlot("Shadow: lights denied", static_cast<int64_t>(frame.light_stats.spot_denied + frame.light_stats.point_denied));

	const auto depth = light_culling::clusterDepthRange(frame.camera_near, frame.camera_far, light_reach);
	frame.cluster_near = depth.near_depth;
	frame.cluster_far = depth.far_depth;
}

}
