/// @file vulkan_renderer_scene.cpp
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

auto probeCacheUri(const toast::ReflectionProbe& probe) -> std::string {
	return std::format("cache://probes/{}.tprobe", static_cast<std::string>(probe.uid()));
}

auto irradianceCacheUri(const toast::IrradianceVolume& volume) -> std::string {
	return std::format("cache://irradiance/{}.tsh", static_cast<std::string>(volume.uid()));
}

auto sameGrid(const ShGridKey& a, const ShGridKey& b) -> bool {
	constexpr float k_tolerance = 1e-3f;
	return a.counts == b.counts && glm::all(glm::lessThan(glm::abs(a.min_corner - b.min_corner), glm::vec3(k_tolerance))) &&
	       glm::all(glm::lessThan(glm::abs(a.extents - b.extents), glm::vec3(k_tolerance)));
}

assets::Handle<assets::Mesh> g_camera_gizmo_mesh;

bool g_camera_gizmo_resolved = false;

std::array<assets::Handle<assets::Texture>, 5> g_light_icons;

std::array<bool, 5> g_light_icons_resolved {};

auto cameraGizmoMesh() -> const assets::Handle<assets::Mesh>& {
	static constexpr std::string_view k_uri = "core://meshes/ToastEngineCam.tmesh";

	if (!g_camera_gizmo_resolved) {
		g_camera_gizmo_resolved = true;
		if (const auto uid = assets::resolveURI(k_uri); uid.has_value()) {
			g_camera_gizmo_mesh = assets::load<assets::Mesh>(*uid);
		} else {
			TOAST_WARN("Render", "Camera gizmo mesh '{}' not found in the asset manifest", k_uri);
		}
	}
	return g_camera_gizmo_mesh;
}

auto lightIcon(toast::LightType type) -> const assets::Handle<assets::Texture>& {
	static constexpr std::array<std::string_view, 5> k_uris {
	  "",
	  "core://textures/PointLight.ktx2",
	  "core://textures/DirectionalLight.ktx2",
	  "core://textures/SpotLight.ktx2",
	  "core://textures/OmniLight.ktx2",
	};

	static_assert(k_uris.size() == g_light_icons.size(), "icon cache must cover every LightType");

	const auto index = static_cast<size_t>(type);
	if (index >= k_uris.size()) {
		return g_light_icons[0];
	}

	if (!g_light_icons_resolved[index]) {
		g_light_icons_resolved[index] = true;
		if (!k_uris[index].empty()) {
			if (const auto uid = assets::resolveURI(k_uris[index]); uid.has_value()) {
				g_light_icons[index] = assets::load<assets::Texture>(*uid);
			} else {
				TOAST_WARN("Render", "Light debug icon '{}' not found in the asset manifest", k_uris[index]);
			}
		}
	}
	return g_light_icons[index];
}

auto pickSkin(const toast::MeshNode& node) -> const assets::Skin* {
	const auto& skin_animation = node.getSkinAnimation();
	if (!skin_animation.hasValue()) {
		return nullptr;
	}

	const auto& skins = skin_animation->skins();
	if (skins.empty()) {
		return nullptr;
	}

	const auto& skin_name = node.getSkinName();
	if (skin_name.empty()) {
		return &skins.front();
	}

	for (const auto& skin : skins) {
		if (skin.name == skin_name) {
			return &skin;
		}
	}
	return nullptr;
}

auto findAncestorAnimationPlayer(toast::Node& node) -> toast::AnimationPlayer* {
	toast::Box<toast::Node> current = node.parent();
	while (current.exists()) {
		if (auto player = current.as<toast::AnimationPlayer>(); player.exists()) {
			return &*player;
		}
		current = current->parent();
	}
	return nullptr;
}

using SkinPoseKey = std::pair<const toast::AnimationPlayer*, const assets::Skin*>;

using SkinPoseCache = std::map<SkinPoseKey, std::pair<uint32_t, uint32_t>>;

void resolveSkinning(
    toast::MeshNode& node, RenderFrame& frame, uint32_t& joint_offset, uint32_t& joint_count, bool& pool_exhausted_warned,
    SkinPoseCache& pose_cache
) {
	const assets::Skin* skin = pickSkin(node);
	if (skin == nullptr) {
		return;
	}

	toast::AnimationPlayer* player = findAncestorAnimationPlayer(node);
	if (player == nullptr) {
		return;
	}

	if (const auto cached = pose_cache.find({player, skin}); cached != pose_cache.end()) {
		joint_offset = cached->second.first;
		joint_count = cached->second.second;
		return;
	}

	const auto joint_world_transforms = player->jointWorldTransforms(*skin);
	const auto joint_matrices = skin->jointMatrices(joint_world_transforms);
	if (joint_matrices.empty()) {
		return;
	}

	if (frame.joint_matrices.size() + joint_matrices.size() > k_max_joint_matrices) {
		if (!pool_exhausted_warned) {
			pool_exhausted_warned = true;
			TOAST_WARN(
			    "Render",
			    "Joint matrix pool ({} matrices/frame) is full; some skinned meshes will draw in bind pose this frame",
			    k_max_joint_matrices
			);
		}
		return;
	}

	joint_offset = static_cast<uint32_t>(frame.joint_matrices.size());
	joint_count = static_cast<uint32_t>(joint_matrices.size());
	frame.joint_matrices.insert(frame.joint_matrices.end(), joint_matrices.begin(), joint_matrices.end());
	pose_cache.emplace(SkinPoseKey {player, skin}, std::pair {joint_offset, joint_count});
}

/// Float in [1 2) whose mantissa hashes the light so ReSTIR finds it after rebuilds
[[nodiscard]]
auto lightIdBits(uint64_t key) -> float {
	uint64_t hash = key * 0x9E3779B97F4A7C15ull;
	hash ^= hash >> 29u;
	return std::bit_cast<float>(static_cast<uint32_t>(hash & 0x007FFFFFull) | 0x3F800000u);
}

/// Rect width axis perpendicular to the facing even when up is parallel to it
[[nodiscard]]
auto areaTangent(const glm::vec3& forward, const glm::vec3& up) -> glm::vec3 {
	const glm::vec3 side = glm::cross(forward, up);
	if (glm::dot(side, side) > 1e-8f) {
		return glm::normalize(side);
	}
	const glm::vec3 fallback = std::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	return glm::normalize(glm::cross(forward, fallback));
}

}

namespace detail {

void clearCachedAssetHandles() {
	g_camera_gizmo_mesh = {};
	g_camera_gizmo_resolved = false;
	g_light_icons = {};
	g_light_icons_resolved = {};
}

}

auto VulkanRenderer::ownerTransform(const toast::Node& node) const -> const glm::mat4* {
	static const glm::mat4 identity(1.0f);
	if (m_render_owner_filter == nullptr || node.owner() == m_render_owner_filter) {
		return &identity;
	}
	if (m_secondary_owner == nullptr || node.owner() != m_secondary_owner) {
		return nullptr;
	}

	// The instance being edited draws from the VoxelEditor not from the level around it
	for (const toast::Node* current = &node; current != nullptr;) {
		if (current == m_secondary_hidden) {
			return nullptr;
		}
		toast::Box<toast::Node> parent = const_cast<toast::Node*>(current)->parent();
		current = parent.exists() ? &*parent : nullptr;
	}
	return &m_secondary_transform;
}

auto VulkanRenderer::blendPostProcessVolumes(const glm::vec3& camera_position) -> PostProcessSettings {
	ZoneScoped;
	PostProcessSettings result = m_post_process_settings;

	auto& volumes = m_tick_post_volumes;
	{
		std::scoped_lock lock(m_reflection_probe_mutex);
		volumes.assign(m_post_process_volume_nodes.begin(), m_post_process_volume_nodes.end());
	}

	std::erase_if(volumes, [this](const toast::PostProcessVolume* volume) {
		return volume == nullptr || (m_render_owner_filter != nullptr && volume->owner() != m_render_owner_filter);
	});

	std::ranges::stable_sort(volumes, {}, [](const toast::PostProcessVolume* volume) { return volume->priority(); });

	for (const auto* volume : volumes) {
		const float influence = volume->influenceAt(camera_position);

		if (!volume->isGlobal()) {
			const glm::vec4 color = influence > 0.0f ? glm::vec4(0.4f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.4f, 0.55f, 0.5f, 1.0f);
			debug::drawOrientedBox(volume->getWorldTransform(), volume->extents(), color);
		}

		if (influence <= 0.0f) {
			continue;
		}
		blendPostProcess(result, volume->settings(), influence);
	}

	return result;
}

void VulkanRenderer::gatherFogVolumes(RenderFrame& frame, const glm::vec3& camera_position) {
	ZoneScoped;
	frame.fog_volumes.clear();
	auto& volumes = m_tick_fog_volumes;
	{
		std::scoped_lock lock(m_reflection_probe_mutex);
		volumes.assign(m_fog_volume_nodes.begin(), m_fog_volume_nodes.end());
	}
	std::erase_if(volumes, [this](const toast::FogVolume* volume) {
		return volume == nullptr || !volume->isEnabled() || volume->density() <= 0.0f ||
		       (m_render_owner_filter != nullptr && volume->owner() != m_render_owner_filter);
	});

	for (const auto* volume : volumes) {
		volume->syncTransform();
	}
	const auto centre = [](const toast::FogVolume* volume) { return glm::vec3(volume->getWorldTransform()[3]); };
	std::ranges::sort(volumes, {}, [&](const toast::FogVolume* volume) { return glm::distance(centre(volume), camera_position); });
	if (volumes.size() > k_max_fog_volumes) {
		volumes.resize(k_max_fog_volumes);
	}

	frame.fog_volumes.reserve(volumes.size());
	for (const auto* volume : volumes) {
		debug::drawOrientedBox(volume->getWorldTransform(), volume->extents(), glm::vec4(0.6f, 0.75f, 0.9f, 1.0f));
		frame.fog_volumes.push_back(
		    FogVolumeGpu {
		      .world_to_local = glm::inverse(volume->getWorldTransform()),
		      .extents_fade = glm::vec4(glm::abs(volume->extents()), std::max(volume->edgeFade(), 0.0f)),
		      .albedo_density = glm::vec4(volume->albedo(), volume->density()),
		      .emission_noise = glm::vec4(volume->emission(), std::clamp(volume->noiseStrength(), 0.0f, 1.0f)),
		      .wind_scale = glm::vec4(volume->wind(), std::max(volume->noiseScale(), 0.05f)),
		      .phase = glm::vec4(std::clamp(volume->anisotropy(), -0.9f, 0.9f), std::max(volume->ambient(), 0.0f), 0.0f, 0.0f),
		      .centre = glm::vec4(glm::vec3(volume->getWorldTransform()[3]), 1.0f),
		    }
		);
	}
}

void VulkanRenderer::tick(float time) noexcept {
	ZoneScopedN("VulkanRenderer::tick()");
	toast::PhaseZones phases;
	TOAST_PHASE(phases, "Tick: settings");

	// Queued by render thread UI that holds the pass lock
	std::vector<std::pair<std::string, toast::settings::Value>> setting_requests;
	{
		const std::lock_guard lock(m_setting_request_mutex);
		setting_requests.swap(m_setting_requests);
	}
	for (auto& [key, value] : setting_requests) {
		toast::settings::Settings::get().setValue(key, value, true);
	}

	if (m_rendering_paused.load(std::memory_order_relaxed)) {
		getCurrentFrameBuild().debug_line_vertices.clear();
		return;
	}

	TOAST_PHASE(phases, "Tick: wait frame slot");
	if (m_frame_slot == FrameSlot::none) {
		acquireFrameSlot();
	}
	const bool got_slot = std::exchange(m_frame_slot, FrameSlot::none) == FrameSlot::held;
	m_perf_ticks.fetch_add(1, std::memory_order_relaxed);

	if (!got_slot) {
		m_skipped_builds.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	m_perf_frames_built.fetch_add(1, std::memory_order_relaxed);

	const auto build_start = std::chrono::steady_clock::now();
	const auto count_build = [this, build_start] {
		m_perf_build_ns.fetch_add(
		    static_cast<uint64_t>(
		        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - build_start).count()
		    ),
		    std::memory_order_relaxed
		);
	};

	TOAST_PHASE(phases, "Tick: reset frame");
	auto& frame = getCurrentFrameBuild();
	resetFrameBuild(frame);

	if (!m_camera) {
		if (m_ui_frame_builder) {
			m_ui_frame_builder(frame);
		}
		frame.ui_world_panels.clear();
		count_build();
		submitFrame();
		return;
	}

	TOAST_PHASE(phases, "Tick: camera and volumes");
	m_camera->syncTransform();
	frame.post_process = blendPostProcessVolumes(m_camera->world_position);
	gatherFogVolumes(frame, m_camera->world_position);
	// GI reads the media too so they go out whether or not the volumetrics pass draws them
	const auto& fog = frame.post_process.fog;
	const bool fog_on = volumetrics();
	frame.frame_data.fog_medium = glm::vec4(
	    fog_on ? fog.density : 0.0f, fog.height, fog.height_falloff, fog_on ? static_cast<float>(frame.fog_volumes.size()) : 0.0f
	);
	frame.frame_data.fog_reach = glm::vec4(fog.distance, volumetricDistance(), static_cast<float>(voxelSunReach()), 0.0f);

	TOAST_PHASE(phases, "Tick: snapshot nodes");
	auto& mesh_nodes_snapshot = m_tick_mesh_nodes;
	{
		std::scoped_lock lock(m_mesh_proxy_mutex);
		mesh_nodes_snapshot.assign(m_mesh_proxy_nodes.begin(), m_mesh_proxy_nodes.end());
	}
	frame.mesh_instances.reserve(mesh_nodes_snapshot.size());

	auto& light_nodes_snapshot = m_tick_light_nodes;
	{
		std::scoped_lock lock(m_light_proxy_mutex);
		light_nodes_snapshot.assign(m_light_proxy_nodes.begin(), m_light_proxy_nodes.end());
	}
	frame.lights.clear();
	frame.light_stats = {};

	TOAST_PHASE(phases, "Tick: mesh proxies");
	gatherMeshProxies(frame);

	TOAST_PHASE(phases, "Tick: voxel proxies");
	buildVoxelProxies(frame);

	TOAST_PHASE(phases, "Tick: camera frame data");
	const auto extent = m_output_target->getExtent();
	const auto render_extent = renderExtent();
	beginDebugLineCollection(frame);
	{
		std::scoped_lock lock(m_mesh_proxy_mutex);
		for (const auto& [node, draw] : m_debug_nodes) {
			if (node->enabled() && node->participatesIn(toast::NodeOwnerParticipation::render)) {
				draw(*node);
			}
		}
	}
	const float aspect =
	    extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : (1080.0f / 720.0f);

	// A camera below a physics body follows the same blended pose as the meshes around it
	const glm::mat4 camera_pose_delta = physics::Simulator::renderPoseDeltaFor(*m_camera);
	const glm::mat4 camera_view = m_camera->getView() * glm::inverse(camera_pose_delta);
	const glm::mat4 camera_projection = m_camera->getProjection(aspect);

	frame.frame_data = FrameUBO {
	  .view = camera_view,
	  .projection = camera_projection,
	  .view_projection = camera_projection * camera_view,
	  .camera_position = glm::vec3(camera_pose_delta * glm::vec4(m_camera->world_position, 1.0f)),
	  .time = time,
	  .render_mode_pad = glm::uvec4(frame.render_mode, 0, 0, 0),
	};

	{
		const EditorOverlays& overlays = editorOverlays();
		frame.frame_data.render_mode_pad.y =
		    (overlays.surface_unit_grid ? 1u : 0u) | (overlays.surface_voxel_grid ? 2u : 0u) | (overlays.voxel_edges ? 4u : 0u);
	}

	TOAST_PHASE(phases, "Tick: reflection probes");
	gatherReflectionProbes(frame, render_extent);

	TOAST_PHASE(phases, "Tick: viewport and debug draws");
	frame.viewport_extent = glm::vec2(static_cast<float>(render_extent.width), static_cast<float>(render_extent.height));
	frame.output_extent = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
	frame.camera_near = m_camera->near_plane;
	frame.camera_far = m_camera->far_plane;

	// Cameras debug draws
	{
		auto& camera_nodes_snapshot = m_tick_camera_nodes;
		{
			std::scoped_lock lock(m_camera_proxy_mutex);
			camera_nodes_snapshot.assign(m_camera_proxy_nodes.begin(), m_camera_proxy_nodes.end());
		}

		for (auto* camera : camera_nodes_snapshot) {
			if (camera == nullptr || !camera->enabled() || camera == m_camera) {
				continue;
			}
			if (m_render_owner_filter != nullptr && camera->owner() != m_render_owner_filter) {
				continue;
			}
			camera->syncTransform();
			debug::drawMesh(cameraGizmoMesh(), camera->getWorldTransform());

			constexpr float k_camera_frustum_length = 3.0f;
			const auto camera_extent = m_output_target->getExtent();    // TODO this might not always be the case
			const float camera_aspect =
			    camera_extent.height > 0 ? static_cast<float>(camera_extent.width) / static_cast<float>(camera_extent.height) : 1.0f;
			debug::drawFrustum(*camera, camera_aspect, glm::vec4(1.0f, 1.0f, 0.2f, 1.0f), k_camera_frustum_length);
		}
	}

	TOAST_PHASE(phases, "Tick: lights");
	auto [ambient_sum, directional_count, shadow_caster_index, shadow_caster_direction] = gatherLights(frame);

	TOAST_PHASE(phases, "Tick: light uniforms");
	frame.frame_data.ambient_color_intensity = glm::vec4(ambient_sum, 1.0f);
	frame.frame_data.directional_light_count_pad.x = directional_count;

	writeProbeUniforms(frame);

	TOAST_PHASE(phases, "Tick: environment");
	if (m_environment_pass != nullptr && m_environment_pass->isReady()) {
		frame.frame_data.environment_params.x = 1.0f;
		frame.frame_data.environment_params.y = static_cast<float>(m_environment_pass->getPrefilteredMipCount());
	}

	logLightingInputs(frame, ambient_sum, directional_count);

	// Voxels are not in the TLAS so tracing would erase their shadows
	// TODO remove once voxel visibility can be traced
	const bool voxels_block_tracing = !frame.voxel_instances.empty();
	const bool trace_shadows = m_core->isRayTracingSupported() && tracedShadowsEnabled() && !voxels_block_tracing &&
	                           std::ranges::any_of(frame.mesh_instances, detail::isTraceable);
	if (tracedShadowsEnabled() && voxels_block_tracing && !m_traced_shadows_voxel_fallback_logged) {
		TOAST_WARN("Render", "Traced shadows fall back to shadow maps while voxel volumes are drawn");
	}
	m_traced_shadows_voxel_fallback_logged = tracedShadowsEnabled() && voxels_block_tracing;
	frame.frame_data.traced_shadow_params.x = trace_shadows ? 1.0f : 0.0f;
	// Voxel volumes join the TLAS so marched voxel rays use ray queries
	frame.frame_data.traced_shadow_params.y =
	    m_core->isRayTracingSupported() && ShaderCompiler::traceRayQuery() && !frame.voxel_instances.empty() ? 1.0f : 0.0f;

	if (!trace_shadows) {
		fitShadowViews(frame, aspect, shadow_caster_index, shadow_caster_direction, m_tick_spot_candidates, m_tick_point_candidates);
	} else {
		releaseShadowSlots();
	}

	TOAST_PHASE(phases, "Tick: finalize lights");
	finalizeLights(frame);

	TOAST_PHASE(phases, "Tick: gizmos and post process");
	copyGizmoState(frame);

	cullAndBuildInstances(frame);

	TOAST_PHASE(phases, "Tick: antialiasing");
	updateTemporalAA(frame, render_extent);

	TOAST_PHASE(phases, "Tick: ui frame");
	if (m_ui_frame_builder) {
		m_ui_frame_builder(frame);
	}

	endDebugLineCollection();
	count_build();
	TracyPlot("Tick: lights", static_cast<int64_t>(frame.lights.size()));
	TracyPlot("Tick: mesh instances", static_cast<int64_t>(frame.mesh_instances.size()));
	TracyPlot("Tick: voxel instances", static_cast<int64_t>(frame.voxel_instances.size()));
	TracyPlot("Tick: shadow views", static_cast<int64_t>(frame.shadows.views.size()));
	TOAST_PHASE(phases, "Tick: submit frame");
	submitFrame();
}

void VulkanRenderer::resetFrameBuild(RenderFrame& frame) {
	frame.mesh_instances.clear();
	frame.material_ranges.clear();
	frame.voxel_instances.clear();
	frame.voxel_storage.reset();
	frame.probe_capture_index = -1;
	frame.probe_capture_face = 0;
	frame.irradiance_capture_index = -1;
	frame.irradiance_capture_face = 0;
	frame.capture_extent = 0;

	frame.asset_refs.clear();
	frame.joint_matrices.clear();
	frame.instance_data.clear();
	frame.shadow_instance_data.clear();
	frame.debug_line_vertices.clear();
	frame.debug_triangle_vertices.clear();
	frame.debug_gizmo_instances.clear();
	frame.debug_billboards.clear();
	frame.debug_meshes.clear();
	frame.transform_gizmo = TransformGizmoDraw {};
	frame.ui_command_buffers.clear();
	frame.ui_output_views.clear();
	frame.ui_world_panels.clear();
	frame.ui_slot_guard.reset();
	frame.frame_data = {};
	frame.shadows.views.clear();
	frame.shadows.matrices.clear();

#if defined(TOAST_DEV_TOOLS)
	frame.imgui_input.mouse_pos = m_imgui_mouse_pos;
	frame.imgui_input.mouse_down = m_imgui_mouse_down;
	frame.imgui_input.mouse_wheel_x = m_imgui_wheel_x_accum;
	frame.imgui_input.mouse_wheel_y = m_imgui_wheel_y_accum;
	frame.imgui_input.key_events = std::move(m_imgui_key_events_accum);
	frame.imgui_input.char_events = std::move(m_imgui_char_events_accum);
	m_imgui_wheel_x_accum = 0.0f;
	m_imgui_wheel_y_accum = 0.0f;
	m_imgui_key_events_accum.clear();
	m_imgui_char_events_accum.clear();
#endif

	frame.sequence = ++m_frame_sequence_counter;
	frame.render_mode = m_render_mode;

	{
		const std::lock_guard lock(m_pending_post_settings_mutex);
		if (m_pending_post_settings.has_value()) {
			m_post_process_settings = *m_pending_post_settings;
			m_pending_post_settings.reset();
		}
	}
	frame.post_process = m_post_process_settings;
}

void VulkanRenderer::gatherMeshProxies(RenderFrame& frame) {
	const auto& mesh_nodes_snapshot = m_tick_mesh_nodes;
	SkinPoseCache skin_pose_cache;

	for (auto* node : mesh_nodes_snapshot) {
		if (node == nullptr || !node->enabled() || !node->participatesIn(toast::NodeOwnerParticipation::render)) {
			continue;
		}
		const glm::mat4* owner_transform = ownerTransform(*node);
		if (owner_transform == nullptr) {
			continue;
		}

		auto& mesh_handle = node->getMesh();
		if (!mesh_handle.hasValue()) {
			continue;
		}

		auto& gpu_mesh = mesh_handle->gpuMesh();
		if (!gpu_mesh.isReady()) {
			continue;
		}

		auto& material_handle = node->getMaterial();

		assets::Material* material = material_handle.hasValue() ? &material_handle.get() : nullptr;
		if (material == nullptr) {
			if (!m_default_material.hasValue()) {
				m_default_material = assets::load<assets::Material>("core://material/default.tmat");
			}
			if (m_default_material.hasValue()) {
				material = &m_default_material.get();
			} else if (!m_default_material_warned) {
				m_default_material_warned = true;
				TOAST_WARN("Render", "Default material core://material/default.tmat not found; meshes without a material are skipped");
			}
		}
		if (material == nullptr) {
			continue;
		}

		// Skinned vertices are posed from joint matrices at the tick pose so they keep it
		const glm::mat4 pose_delta = gpu_mesh.isSkinned() ? glm::mat4(1.0f) : physics::Simulator::renderPoseDeltaFor(*node);
		const auto world_transform = *owner_transform * pose_delta * node->worldTransformForRender();

		uint32_t joint_offset = 0;
		uint32_t joint_count = 0;
		if (gpu_mesh.isSkinned()) {
			resolveSkinning(*node, frame, joint_offset, joint_count, m_joint_matrix_pool_exhausted_warned, skin_pose_cache);
		}

		const glm::vec4 local_sphere = mesh_handle->boundingSphere();
		glm::vec3 world_center = glm::vec3(world_transform * glm::vec4(glm::vec3(local_sphere), 1.0f));
		const glm::vec3 scale {
		  glm::length(glm::vec3(world_transform[0])),
		  glm::length(glm::vec3(world_transform[1])),
		  glm::length(glm::vec3(world_transform[2])),
		};
		float world_radius = local_sphere.w * std::max({scale.x, scale.y, scale.z});

		if (joint_count > 0 && joint_offset + joint_count <= frame.joint_matrices.size()) {
			glm::vec3 minimum(std::numeric_limits<float>::max());
			glm::vec3 maximum(std::numeric_limits<float>::lowest());
			for (uint32_t j = 0; j < joint_count; ++j) {
				const glm::vec3 joint_position = glm::vec3(frame.joint_matrices[joint_offset + j][3]);
				minimum = glm::min(minimum, joint_position);
				maximum = glm::max(maximum, joint_position);
			}

			world_center = (minimum + maximum) * 0.5f;
			world_radius = (glm::length(maximum - minimum) * 0.5f) + (local_sphere.w * std::max({scale.x, scale.y, scale.z}));
		}

		frame.asset_refs.push_back(mesh_handle);
		if (material_handle.hasValue()) {
			frame.asset_refs.push_back(material_handle);
		}

		frame.mesh_instances.push_back(
		    MeshInstanceProxy {
		      .mesh = &gpu_mesh,
		      .material = material,
		      .root_material = material->rootMaterial(),
		      .model = world_transform,
		      .joint_offset = joint_offset,
		      .joint_count = joint_count,
		      .bounds_center = world_center,
		      .bounds_radius = world_radius,
		      .node_rid = node->rid(),
		      .outlined = isOutlined(node->rid()),
		    }
		);
	}
}

void VulkanRenderer::gatherReflectionProbes(RenderFrame& frame, vk::Extent2D render_extent) {
	std::scoped_lock lock(m_reflection_probe_mutex);
	const auto probe_count = static_cast<int32_t>(std::min<size_t>(m_reflection_probe_nodes.size(), k_max_reflection_probes));

	if (m_probe_bake_requested.exchange(false, std::memory_order_acq_rel) && probe_count > 0) {
		m_probe_bake_cursor = 0;
		TOAST_INFO("Render", "Baking {} reflection probe(s), {} frames", probe_count, probe_count * 6);
	}

	uint32_t stale = 0;
	bool any_unbaked = false;
	if (m_reflection_probe_pass != nullptr) {
		for (int32_t i = 0; i < probe_count; ++i) {
			const auto index = static_cast<uint32_t>(i);

			if (!m_reflection_probe_pass->isBaked(index) && m_reflection_probe_nodes[i]->isPrebaked() &&
			    !m_probe_load_attempted.contains(m_reflection_probe_nodes[i])) {
				m_probe_load_attempted.insert(m_reflection_probe_nodes[i]);
				if (m_reflection_probe_pass->loadProbe(index, probeCacheUri(*m_reflection_probe_nodes[i]))) {
					m_reflection_probe_pass->setBakedTransform(
					    index, m_reflection_probe_nodes[i]->world_position, m_reflection_probe_nodes[i]->boxExtents()
					);
				}
			}

			if (!m_reflection_probe_pass->isBaked(index)) {
				any_unbaked = true;
			}
			if (m_reflection_probe_pass->isStale(
			        index, m_reflection_probe_nodes[i]->world_position, m_reflection_probe_nodes[i]->boxExtents()
			    ) ||
			    m_reflection_probe_pass->getProbeResolution(index) != m_reflection_probe_nodes[i]->resolution()) {
				++stale;
			}
		}
	}
	m_probe_stale_count.store(stale, std::memory_order_relaxed);

	if (any_unbaked && m_probe_bake_cursor < 0 && !m_probe_auto_baked) {
		m_probe_auto_baked = true;
		m_probe_bake_cursor = 0;
	}

	if (m_probe_bake_cursor >= 0) {
		if (m_probe_bake_cursor >= probe_count * 6) {
			m_probe_bake_cursor = -1;
			TOAST_INFO("Render", "Reflection probe bake finished");
		} else {
			const int32_t probe_index = m_probe_bake_cursor / 6;
			const auto face = static_cast<uint32_t>(m_probe_bake_cursor % 6);
			const glm::vec3 probe_position = m_reflection_probe_nodes[probe_index]->world_position;

			const auto basis = cubeFaceBasis(face);
			const glm::mat4 capture_view = glm::lookAt(probe_position, probe_position + basis.forward, basis.up);

			// Not Y flipped unlike Camera::getProjection()
			const glm::mat4 capture_projection =
			    depth::perspective(glm::radians(90.0f), 1.0f, m_camera->near_plane, m_camera->far_plane);

			frame.frame_data.view = capture_view;
			frame.frame_data.projection = capture_projection;
			frame.frame_data.view_projection = capture_projection * capture_view;
			frame.frame_data.camera_position = probe_position;
			frame.probe_capture_index = probe_index;
			frame.probe_capture_face = face;
			frame.capture_extent =
			    std::min({m_reflection_probe_nodes[probe_index]->resolution(), render_extent.width, render_extent.height});

			frame.render_mode = 0;
			frame.frame_data.render_mode_pad.x = 0;

			if (face == 0 && m_reflection_probe_pass != nullptr) {
				m_reflection_probe_pass->setProbeResolution(
				    static_cast<uint32_t>(probe_index), m_reflection_probe_nodes[probe_index]->resolution()
				);
				m_reflection_probe_pass->setBakedTransform(
				    static_cast<uint32_t>(probe_index), probe_position, m_reflection_probe_nodes[probe_index]->boxExtents()
				);
			}

			if (face == 5 && m_reflection_probe_pass != nullptr) {
				m_reflection_probe_pass->queueSave(
				    static_cast<uint32_t>(probe_index), probeCacheUri(*m_reflection_probe_nodes[probe_index])
				);
			}

			++m_probe_bake_cursor;
		}
	}

	{
		uint32_t base = 0;
		uint32_t written = 0;
		uint32_t stale_volumes = 0;

		m_irradiance_published_nodes.clear();

		for (auto* volume : m_irradiance_volume_nodes) {
			if (written >= k_max_irradiance_volumes) {
				break;
			}
			if (volume == nullptr) {
				continue;
			}
			if (m_render_owner_filter != nullptr && volume->owner() != m_render_owner_filter) {
				continue;
			}
			volume->syncTransform();

			const glm::uvec3 counts = volume->probeCounts();
			const uint32_t count = counts.x * counts.y * counts.z;
			if (base + count > k_max_irradiance_probes) {
				TOAST_WARN("Render", "Irradiance volume '{}' needs {} probes and does not fit; skipped", volume->name(), count);
				continue;
			}

			const glm::vec3 min_corner = volume->world_position - volume->extents();
			const ShGridKey current_key {.min_corner = min_corner, .extents = volume->extents(), .counts = counts};

			const auto baked_it = m_irradiance_baked_keys.find(volume);
			const bool baked = baked_it != m_irradiance_baked_keys.end() && sameGrid(baked_it->second, current_key);
			if (!baked) {
				++stale_volumes;
			}

			frame.frame_data.irradiance_volumes[written] = {
			  .min_corner_spacing = glm::vec4(min_corner, volume->spacing()),
			  .counts_base = glm::uvec4(counts, base),
			  .extents_intensity = glm::vec4(volume->extents(), volume->intensity()),
			  .baked_pad = glm::uvec4(baked ? 1u : 0u, 0u, 0u, 0u)
			};

			if (m_reflection_probe_pass != nullptr && !m_irradiance_load_attempted.contains(volume)) {
				m_irradiance_load_attempted.insert(volume);
				if (m_reflection_probe_pass->loadShRange(base, count, irradianceCacheUri(*volume), current_key)) {
					m_irradiance_baked_keys[volume] = current_key;
					frame.frame_data.irradiance_volumes[written].baked_pad.x = 1;
					--stale_volumes;
				}
			}

			m_irradiance_published_nodes.push_back(volume);
			base += count;
			++written;
		}

		frame.frame_data.irradiance_volume_count_pad.x = written;
		m_irradiance_probe_total = base;
		m_irradiance_stale_count.store(stale_volumes, std::memory_order_relaxed);

		for (uint32_t v = 0; v < written; ++v) {
			const auto& data = frame.frame_data.irradiance_volumes[v];
			const glm::vec3 min_corner(data.min_corner_spacing);
			const glm::vec3 extents(data.extents_intensity);
			const glm::uvec3 counts(data.counts_base);

			debug::drawBox(min_corner, min_corner + extents * 2.0f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));

			const glm::vec3 step = (extents * 2.0f) / glm::max(glm::vec3(counts) - 1.0f, glm::vec3(1.0f));

			const uint32_t total = counts.x * counts.y * counts.z;
			const uint32_t stride = total > 512 ? (total / 512) + 1 : 1;

			const float dot_radius = std::min({step.x, step.y, step.z}) * 0.08f;
			for (uint32_t i = 0; i < total; i += stride) {
				const glm::uvec3 grid(i % counts.x, (i / counts.x) % counts.y, i / (counts.x * counts.y));
				debug::drawSphere(min_corner + glm::vec3(grid) * step, dot_radius, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
			}
		}
	}

	if (m_irradiance_bake_requested.exchange(false, std::memory_order_acq_rel) && m_irradiance_probe_total > 0) {
		m_irradiance_bake_cursor = 0;
		TOAST_INFO("Render", "Baking {} irradiance probe(s), {} frames", m_irradiance_probe_total, m_irradiance_probe_total * 6);
	}

	if (m_probe_bake_cursor < 0 && m_irradiance_bake_cursor >= 0 && m_reflection_probe_pass != nullptr) {
		const auto total_faces = static_cast<int32_t>(m_irradiance_probe_total) * 6;
		if (m_irradiance_bake_cursor >= total_faces) {
			m_irradiance_bake_cursor = -1;
			TOAST_INFO("Render", "Irradiance volume bake finished ({} probes)", m_irradiance_probe_total);

			for (uint32_t v = 0; v < frame.frame_data.irradiance_volume_count_pad.x; ++v) {
				const auto& data = frame.frame_data.irradiance_volumes[v];
				const glm::uvec3 counts(data.counts_base);
				if (v >= m_irradiance_published_nodes.size() || m_irradiance_published_nodes[v] == nullptr) {
					continue;
				}
				auto* baked_volume = m_irradiance_published_nodes[v];

				const ShGridKey key {
				  .min_corner = glm::vec3(data.min_corner_spacing), .extents = glm::vec3(data.extents_intensity), .counts = counts
				};

				m_irradiance_baked_keys[baked_volume] = key;

				m_reflection_probe_pass->queueShSave(
				    data.counts_base.w, counts.x * counts.y * counts.z, irradianceCacheUri(*baked_volume), key
				);
			}
		} else {
			const int32_t probe_index = m_irradiance_bake_cursor / 6;
			const auto face = static_cast<uint32_t>(m_irradiance_bake_cursor % 6);

			glm::vec3 probe_position(0.0f);
			for (uint32_t v = 0; v < frame.frame_data.irradiance_volume_count_pad.x; ++v) {
				const auto& data = frame.frame_data.irradiance_volumes[v];
				const glm::uvec3 counts(data.counts_base);
				const auto count = static_cast<int32_t>(counts.x * counts.y * counts.z);
				const auto volume_base = static_cast<int32_t>(data.counts_base.w);

				if (probe_index < volume_base || probe_index >= volume_base + count) {
					continue;
				}

				const int32_t local = probe_index - volume_base;
				const glm::ivec3 grid(
				    local % static_cast<int32_t>(counts.x),
				    (local / static_cast<int32_t>(counts.x)) % static_cast<int32_t>(counts.y),
				    local / static_cast<int32_t>(counts.x * counts.y)
				);

				const glm::vec3 extents(data.extents_intensity);
				const glm::vec3 step = (extents * 2.0f) / glm::max(glm::vec3(counts) - 1.0f, glm::vec3(1.0f));
				probe_position = glm::vec3(data.min_corner_spacing) + glm::vec3(grid) * step;
				break;
			}

			const auto basis = cubeFaceBasis(face);
			frame.frame_data.view = glm::lookAt(probe_position, probe_position + basis.forward, basis.up);
			frame.frame_data.projection = depth::perspective(glm::radians(90.0f), 1.0f, m_camera->near_plane, m_camera->far_plane);
			frame.frame_data.view_projection = frame.frame_data.projection * frame.frame_data.view;
			frame.frame_data.camera_position = probe_position;
			frame.irradiance_capture_index = probe_index;
			frame.irradiance_capture_face = face;

			frame.capture_extent = std::min({k_irradiance_capture_size, render_extent.width, render_extent.height});

			frame.render_mode = 0;
			frame.frame_data.render_mode_pad.x = 0;

			frame.frame_data.irradiance_volume_count_pad.x = 0;

			++m_irradiance_bake_cursor;
		}
	}

	m_bake_active.store(m_probe_bake_cursor >= 0 || m_irradiance_bake_cursor >= 0, std::memory_order_relaxed);
}

auto VulkanRenderer::gatherLights(RenderFrame& frame) -> LightGather {
	const auto& light_nodes_snapshot = m_tick_light_nodes;

	glm::vec3 ambient_sum(0.0f);
	uint32_t directional_count = 0;
	frame.lights.reserve(light_nodes_snapshot.size());

	int32_t shadow_caster_index = -1;
	glm::vec3 shadow_caster_direction {0.0f};

	auto& spot_candidates = m_tick_spot_candidates;
	auto& point_candidates = m_tick_point_candidates;
	spot_candidates.clear();
	point_candidates.clear();
	m_tick_light_meta.clear();

	const glm::mat4& frame_view = frame.frame_data.view;
	const auto frustum = extractFrustumPlanes(frame.frame_data.view_projection, depth::k_reversed_z);

	struct PunctualSubmit {
		GpuLight gpu;
		light_culling::Sphere bounds;
		uint64_t key = 0;
		float cone_fraction = 1.0f;
		float camera_distance = 0.0f;
		float resolution_scale = 1.0f;
		float shadow_distance = 0.0f;
		bool casts_shadows = false;
		/// Marched and sampled lights shadow voxels by marching so they never take a shadow map slot
		bool shadow_map = false;
	};

	const auto submit_punctual = [&](const PunctualSubmit& submit, std::vector<PunctualShadowCandidate>& candidates) {
		++frame.light_stats.submitted;

		// Its whole range is off screen so it neither lights nor shadows a visible pixel
		if (!sphereInFrustum(frustum, submit.bounds.center, submit.bounds.radius)) {
			++frame.light_stats.offscreen;
			return;
		}

		const GpuLight& gpu = submit.gpu;
		const float importance = light_culling::importance(
		    glm::vec3(gpu.color_intensity), gpu.color_intensity.w, gpu.world_pos_range.w, submit.camera_distance, submit.cone_fraction
		);

		if (submit.shadow_map) {
			candidates.push_back(
			    PunctualShadowCandidate {
			      .light_index = frame.lights.size(),
			      .distance_squared = submit.camera_distance * submit.camera_distance,
			      .resolution_scale = submit.resolution_scale,
			      .shadow_distance = submit.shadow_distance,
			      .visible = true,
			      .key = submit.key,
			      .importance = light_culling::shadowPriority(
			          glm::vec3(gpu.color_intensity), gpu.color_intensity.w, submit.camera_distance, submit.cone_fraction
			      ),
			    }
			);
		}

		m_tick_light_meta.push_back(
		    LightMeta {
		      .importance = importance,
		      .depth_reach = -(frame_view * glm::vec4(submit.bounds.center, 1.0f)).z + submit.bounds.radius,
		      .visible = true,
		    }
		);
		frame.lights.push_back(gpu);
	};

	// Marched and sampled shadows trace scene rays
	const bool shadow_maps_only = voxelShadowMapsOnly();
	const auto shadow_mode = [shadow_maps_only](const toast::Light& light) {
		return shadow_maps_only ? toast::LightShadowMode::shadow_map : light.shadowMode();
	};

	// y plus z bounds the emitter so culling widens by it
	const auto area_shape = [&shadow_mode](const toast::Light& light, bool casts_shadows) -> glm::vec4 {
		const float radius = std::max(light.sourceRadius(), 0.0f);
		const float half_length = 0.5f * std::max(light.sourceLength(), 0.0f);
		const float half_width = 0.5f * std::max(light.sourceWidth(), 0.0f);
		const float mode = casts_shadows ? static_cast<float>(shadow_mode(light)) : 0.0f;
		switch (light.shape()) {
			case toast::LightShape::sphere:
			case toast::LightShape::disc: return {static_cast<float>(light.shape()), radius, 0.0f, mode};
			case toast::LightShape::capsule: return {static_cast<float>(light.shape()), radius, half_length, mode};
			case toast::LightShape::rect: return {static_cast<float>(light.shape()), half_width, half_length, mode};
			case toast::LightShape::point: break;
		}
		return {0.0f, 0.0f, 0.0f, mode};
	};

	for (auto* light : light_nodes_snapshot) {
		if (light == nullptr || !light->enabled()) {
			continue;
		}
		if (m_render_owner_filter != nullptr && light->owner() != m_render_owner_filter) {
			continue;
		}
		light->syncTransform();

		const float camera_distance = glm::distance(light->world_position, m_camera->world_position);
		const bool positional =
		    light->lightType() != toast::LightType::ambient && light->lightType() != toast::LightType::directional;

		if (positional && light->cullDistance() > 0.0f && camera_distance > light->cullDistance()) {
			continue;
		}

		const uint64_t light_key = light->rid();
		const bool holds_slot = shadow_slots::owns(m_spot_slots, light_key) || shadow_slots::owns(m_point_slots, light_key);
		const float shadow_limit = light->shadowDistance() * (holds_slot ? shadow_slots::k_distance_slack : 1.0f);

		const bool casts_shadows =
		    light->castsShadows() && (!positional || light->shadowDistance() <= 0.0f || camera_distance <= shadow_limit);
		const bool shadow_map = casts_shadows && shadow_mode(*light) == toast::LightShadowMode::shadow_map;

		if (isOutlined(light->rid())) {
			toast::light_gizmo::draw(*light);
		}

		constexpr float k_light_icon_size = 0.5f;
		debug::drawBillboard(
		    light->world_position, k_light_icon_size, lightIcon(light->lightType()), glm::vec4(light->color(), 1.0f)
		);

		switch (light->lightType()) {
			case toast::LightType::ambient: {
				ambient_sum += light->color() * light->intensity();
				break;
			}
			case toast::LightType::directional: {
				if (directional_count >= k_max_directional_lights) {
					break;
				}
				const bool marched_sun = casts_shadows && shadow_mode(*light) != toast::LightShadowMode::shadow_map;
				const float angular_radius = 0.5f * glm::radians(static_cast<const toast::DirectionalLight*>(light)->angularDiameter());
				frame.frame_data.directional_lights[directional_count] = DirectionalLightData {
				  .direction = glm::vec4(light->forward(), marched_sun ? std::max(angular_radius, 1e-6f) : 0.0f),
				  .color_intensity = glm::vec4(light->color(), light->intensity()),
				};
				if (shadow_caster_index < 0 && shadow_map) {
					shadow_caster_index = static_cast<int32_t>(directional_count);
					shadow_caster_direction = light->forward();
				}
				++directional_count;
				break;
			}
			case toast::LightType::point: {
				auto* point = static_cast<toast::PointLight*>(light);
				const glm::vec3 world_pos = point->world_position;
				const glm::vec3 view_pos = glm::vec3(frame_view * glm::vec4(world_pos, 1.0f));
				const glm::vec3 forward = glm::normalize(point->forward());
				const glm::vec4 shape = area_shape(*point, casts_shadows);

				submit_punctual(
				    PunctualSubmit {
				      .gpu =
				          GpuLight {
				                    .world_pos_range = glm::vec4(world_pos, point->attenuation()),
				                    .view_pos_type = glm::vec4(view_pos, 0.0f),
				                    .color_intensity = glm::vec4(point->color(), point->intensity()),
				                    .direction_pad = glm::vec4(forward, -1.0f),
				                    .cone_angles = glm::vec4(0.0f),
				                    .shape = shape,
				                    .shape_tangent = glm::vec4(areaTangent(forward, point->up()), lightIdBits(light_key)),
				                    },
				      .bounds = light_culling::Sphere {world_pos, point->attenuation() + shape.y + shape.z},
				      .key = light_key,
				      .camera_distance = camera_distance,
				      .resolution_scale = point->shadowResolutionScale(),
				      .shadow_distance = light->shadowDistance(),
				      .casts_shadows = casts_shadows,
				      .shadow_map = shadow_map,
				},
				    point_candidates
				);
				break;
			}
			case toast::LightType::spot: {
				auto* spot = static_cast<toast::Spotlight*>(light);
				const glm::vec3 world_pos = spot->world_position;
				const glm::vec3 forward = glm::normalize(spot->forward());
				const glm::vec3 view_pos = glm::vec3(frame_view * glm::vec4(world_pos, 1.0f));
				const glm::vec3 view_axis = glm::normalize(glm::mat3(frame_view) * forward);
				const glm::vec4 shape = area_shape(*spot, casts_shadows);

				// Inner past outer would invert the smoothstep edges
				const float outer = glm::radians(spot->outerRadius());
				const float inner = std::min(glm::radians(spot->innerRadius()), outer);
				light_culling::Sphere spot_bounds = light_culling::spotBounds(world_pos, forward, spot->attenuation(), outer);
				spot_bounds.radius += shape.y + shape.z;

				submit_punctual(
				    PunctualSubmit {
				      .gpu =
				          GpuLight {
				                    .world_pos_range = glm::vec4(world_pos, spot->attenuation()),
				                    .view_pos_type = glm::vec4(view_pos, 1.0f),
				                    .view_direction = glm::vec4(view_axis, 0.0f),
				                    .color_intensity = glm::vec4(spot->color(), spot->intensity()),
				                    .direction_pad = glm::vec4(forward, -1.0f),
				                    .cone_angles = glm::vec4(std::cos(outer), std::cos(inner), 0.0f, 0.0f),
				                    .shape = shape,
				                    .shape_tangent = glm::vec4(areaTangent(forward, spot->up()), lightIdBits(light_key)),
				                    },
				      .bounds = spot_bounds,
				      .key = light_key,
				      .cone_fraction = 0.5f * (1.0f - std::cos(outer)),
				      .camera_distance = camera_distance,
				      .resolution_scale = spot->shadowResolutionScale(),
				      .shadow_distance = light->shadowDistance(),
				      .casts_shadows = casts_shadows,
				      .shadow_map = shadow_map,
				},
				    spot_candidates
				);
				break;
			}
			default: break;
		}
	}

	return {ambient_sum, directional_count, shadow_caster_index, shadow_caster_direction};
}

void VulkanRenderer::writeProbeUniforms(RenderFrame& frame) {
	if (frame.probe_capture_index < 0) {
		auto& probes_snapshot = m_tick_probe_nodes;
		{
			std::scoped_lock lock(m_reflection_probe_mutex);
			probes_snapshot.assign(m_reflection_probe_nodes.begin(), m_reflection_probe_nodes.end());
		}

		const glm::vec3 camera_position = frame.frame_data.camera_position;
		auto& identified = m_tick_probes_by_distance;
		identified.clear();
		identified.reserve(probes_snapshot.size());
		for (uint32_t i = 0; i < probes_snapshot.size(); ++i) {
			if (probes_snapshot[i] == nullptr ||
			    (m_render_owner_filter != nullptr && probes_snapshot[i]->owner() != m_render_owner_filter)) {
				continue;
			}
			identified.emplace_back(i, probes_snapshot[i]);
		}

		const auto distance_squared = [&](const toast::ReflectionProbe* probe) {
			const glm::vec3 delta = probe->world_position - camera_position;
			return glm::dot(delta, delta);
		};
		std::ranges::sort(identified, [&](const auto& a, const auto& b) {
			return distance_squared(a.second) < distance_squared(b.second);
		});

		for (const auto& [cube_index, probe] : identified) {
			const glm::vec4 volume_color(0.35f, 0.8f, 1.0f, 1.0f);
			if (probe->usesBoxProjection()) {
				debug::drawBox(probe->world_position - probe->boxExtents(), probe->world_position + probe->boxExtents(), volume_color);
			} else {
				debug::drawSphere(probe->world_position, probe->influenceRadius(), volume_color);
			}
		}

		uint32_t probe_count = 0;
		for (const auto& [cube_index, probe] : identified) {
			if (probe_count >= k_max_reflection_probes) {
				break;
			}
			frame.frame_data.reflection_probes[probe_count] = ReflectionProbeData {
			  .position_radius = glm::vec4(probe->world_position, probe->influenceRadius()),
			  .box_extents_intensity =
			      glm::vec4(probe->usesBoxProjection() ? probe->boxExtents() : glm::vec3(0.0f), probe->intensity()),
			  .params = glm::vec4(static_cast<float>(cube_index), 0.0f, 0.0f, 0.0f),
			};
			++probe_count;
		}
		frame.frame_data.reflection_probe_count_pad.x = probe_count;
		if (m_reflection_probe_pass != nullptr) {
			frame.frame_data.reflection_probe_count_pad.y = m_reflection_probe_pass->getMipCount();
			uint32_t baked_mask = 0;
			for (uint32_t i = 0; i < k_max_reflection_probes; ++i) {
				if (m_reflection_probe_pass->isBaked(i)) {
					baked_mask |= 1U << i;
				}
			}
			frame.frame_data.reflection_probe_count_pad.z = baked_mask;
		}
	}
}

void VulkanRenderer::logLightingInputs(const RenderFrame& frame, glm::vec3 ambient_sum, uint32_t directional_count) {
	auto state = std::tuple {
	  ambient_sum.r + ambient_sum.g + ambient_sum.b,
	  directional_count,
	  frame.light_stats.submitted,
	  frame.frame_data.environment_params.x,
	  frame.mesh_instances.size(),
	  frame.render_mode,
	  frame.post_process.tonemap.exposure,
	  frame.frame_data.reflection_probe_count_pad.x,
	  frame.frame_data.reflection_probe_count_pad.z,
	};
	static std::optional<decltype(state)> last_state;
	if (!last_state.has_value() || *last_state != state) {
		last_state = state;
		TOAST_TRACE(
		    "Render",
		    "Lighting inputs: ambient=({:.3f}, {:.3f}, {:.3f}), {} directional, {} punctual, environment={}, "
		    "{} mesh instances, render mode {}, exposure {:.3f}, {} probes (baked mask {:#x}),",
		    ambient_sum.r,
		    ambient_sum.g,
		    ambient_sum.b,
		    directional_count,
		    frame.light_stats.submitted,
		    frame.frame_data.environment_params.x >= 0.5f ? "ready" : "absent",
		    frame.mesh_instances.size(),
		    frame.render_mode,
		    frame.post_process.tonemap.exposure,
		    frame.frame_data.reflection_probe_count_pad.x,
		    frame.frame_data.reflection_probe_count_pad.z
		);
	}
}

void VulkanRenderer::copyGizmoState(RenderFrame& frame) {
	// TODO compile out of non editor builds
	if (m_gizmo_state.visible) {
		const float scale = toast::gizmo_layout::k_screen_size * glm::distance(m_camera->world_position, m_gizmo_state.origin);

		frame.transform_gizmo.visible = true;
		frame.transform_gizmo.tool = m_gizmo_state.tool;
		frame.transform_gizmo.model = glm::translate(glm::mat4(1.0f), m_gizmo_state.origin) *
		                              glm::mat4_cast(m_gizmo_state.orientation) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
		frame.transform_gizmo.hover = m_gizmo_state.hover;
		frame.transform_gizmo.active = m_gizmo_state.active;
		frame.transform_gizmo.drag_scale_factor = m_gizmo_state.drag_scale_factor;
	}

	if (m_gizmo_state.size_handle_count > 0) {
		frame.transform_gizmo.size_handles = m_gizmo_state.size_handles;
		frame.transform_gizmo.size_handle_count = m_gizmo_state.size_handle_count;
		frame.transform_gizmo.size_handle_scale = m_gizmo_state.size_handle_scale;
		frame.transform_gizmo.hover = m_gizmo_state.hover;
		frame.transform_gizmo.active = m_gizmo_state.active;
	}
}

void VulkanRenderer::cullAndBuildInstances(RenderFrame& frame) {
	const bool is_capture = frame.probe_capture_index >= 0 || frame.irradiance_capture_index >= 0;

	const bool freeze = m_cull_freeze.load(std::memory_order_relaxed) && !is_capture;
	if (!freeze) {
		m_has_frozen_cull = false;
	} else if (!m_has_frozen_cull) {
		m_frozen_cull_view_projection = frame.frame_data.view_projection;
		m_has_frozen_cull = true;
	}

	const glm::mat4 cull_view_projection = freeze ? m_frozen_cull_view_projection : frame.frame_data.view_projection;
	const auto planes = extractFrustumPlanes(cull_view_projection, depth::k_reversed_z);

	uint32_t visible = 0;
	for (auto& proxy : frame.mesh_instances) {
		proxy.visible = proxy.bounds_radius <= 0.0f || sphereInFrustum(planes, proxy.bounds_center, proxy.bounds_radius);
		visible += proxy.visible ? 1u : 0u;
	}
	m_visible_instance_count.store(visible, std::memory_order_relaxed);

	const glm::vec3 eye = frame.frame_data.camera_position;
	const float near_plane = m_camera->near_plane;
	frame.near_plane = near_plane;
	for (auto& proxy : frame.voxel_instances) {
		proxy.camera_inside = voxel::containsPoint(proxy.inverse_model, proxy.brick_dims, eye, near_plane);
		proxy.visible = proxy.camera_inside || sphereInFrustum(planes, proxy.bounds_center, proxy.bounds_radius);
		proxy.view_distance = std::max(glm::distance(eye, proxy.bounds_center) - proxy.bounds_radius, 0.0f);
	}

	std::ranges::stable_sort(frame.voxel_instances, [](const VoxelVolumeProxy& a, const VoxelVolumeProxy& b) {
		return a.view_distance < b.view_distance;
	});

	std::ranges::stable_sort(frame.mesh_instances, [](const MeshInstanceProxy& a, const MeshInstanceProxy& b) {
		if (a.root_material != b.root_material) {
			return std::less<> {}(a.root_material, b.root_material);
		}
		if (a.material != b.material) {
			return std::less<> {}(a.material, b.material);
		}
		return std::less<> {}(a.mesh, b.mesh);
	});

	frame.material_ranges.clear();

	frame.instance_data.clear();
	frame.instance_data.reserve(frame.mesh_instances.size());
	for (uint32_t i = 0; i < frame.mesh_instances.size(); ++i) {
		auto& proxy_ref = frame.mesh_instances[i];
		if (frame.material_ranges.empty() || frame.material_ranges.back().root_material != proxy_ref.root_material) {
			if (!frame.material_ranges.empty()) {
				frame.material_ranges.back().end = i;
			}
			frame.material_ranges.push_back({.root_material = proxy_ref.root_material, .begin = i, .end = i});
		}
	}
	if (!frame.material_ranges.empty()) {
		frame.material_ranges.back().end = static_cast<uint32_t>(frame.mesh_instances.size());
	}

	uint32_t posed_cursor = 0;
	for (auto& proxy : frame.mesh_instances) {
		proxy.posed_vertex_offset = MeshInstanceProxy::k_no_posed_vertices;

		if (proxy.mesh == nullptr || !proxy.mesh->isSkinned() || proxy.joint_count == 0) {
			continue;
		}

		const uint32_t vertex_count = proxy.mesh->getVertexCount();
		if (vertex_count == 0 || posed_cursor + vertex_count > SkinningPass::k_max_posed_vertices) {
			continue;
		}

		proxy.posed_vertex_offset = posed_cursor;
		posed_cursor += vertex_count;
	}

	const auto instance_model_of = [](const MeshInstanceProxy& proxy) {
		return proxy.posed_vertex_offset == MeshInstanceProxy::k_no_posed_vertices ? proxy.model : glm::mat4(1.0f);
	};

	frame.shadow_instance_data.clear();
	frame.shadow_instance_data.reserve(std::min<size_t>(frame.mesh_instances.size(), k_max_instances));
	for (const auto& proxy : frame.mesh_instances) {
		if (frame.shadow_instance_data.size() >= k_max_instances) {
			break;
		}
		const glm::mat4 model = instance_model_of(proxy);
		frame.shadow_instance_data.push_back(
		    InstanceData {.model = model, .previous_model = model, .joint_offset = proxy.joint_offset}
		);
	}

	std::unordered_map<uint64_t, glm::mat4> drawn_mesh_models;
	for (auto& proxy : frame.mesh_instances) {
		if (!proxy.visible) {
			continue;
		}
		if (frame.instance_data.size() >= k_max_instances) {
			proxy.visible = false;
			continue;
		}

		// Posed vertices have no previous pose so TAA reprojects them from depth
		const bool posed = proxy.posed_vertex_offset != MeshInstanceProxy::k_no_posed_vertices;
		const glm::mat4 model = instance_model_of(proxy);
		const auto previous = m_mesh_previous_models.find(proxy.node_rid);
		const bool has_previous = !posed && proxy.node_rid != 0 && previous != m_mesh_previous_models.end();

		proxy.instance_index = static_cast<uint32_t>(frame.instance_data.size());
		frame.instance_data.push_back(
		    InstanceData {
		      .model = model,
		      .previous_model = has_previous ? previous->second : model,
		      .joint_offset = proxy.joint_offset,
		      .flags = posed ? k_instance_no_motion : 0u,
		    }
		);
		if (!posed && proxy.node_rid != 0) {
			drawn_mesh_models.insert_or_assign(proxy.node_rid, model);
		}
	}
	if (!is_capture) {
		m_mesh_previous_models.swap(drawn_mesh_models);
	}

	if (m_cull_debug_draw.load(std::memory_order_relaxed) && !is_capture) {
		debug::drawFrustumFromMatrix(cull_view_projection, glm::vec4(1.0f, 1.0f, 0.2f, 1.0f));

		constexpr int k_debug_sphere_segments = 8;
		for (const auto& proxy : frame.mesh_instances) {
			if (proxy.bounds_radius <= 0.0f) {
				continue;
			}
			const glm::vec4 color = proxy.visible ? glm::vec4(0.2f, 1.0f, 0.3f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.2f, 1.0f);
			debug::drawSphere(proxy.bounds_center, proxy.bounds_radius, color, k_debug_sphere_segments);
		}

		for (const auto& proxy : frame.voxel_instances) {
			const glm::vec4 color = proxy.visible ? glm::vec4(0.2f, 0.85f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.2f, 1.0f);
			debug::drawSphere(proxy.bounds_center, proxy.bounds_radius, color, k_debug_sphere_segments);
		}
	}
}

void VulkanRenderer::updateTemporalAA(RenderFrame& frame, vk::Extent2D render_extent) {
	const bool is_capture = frame.probe_capture_index >= 0 || frame.irradiance_capture_index >= 0;
	const bool taa = taaEnabled() && !is_capture && frame.render_mode == 0;
	const bool same_camera = m_taa_history.has_previous && m_taa_history.camera == m_camera;

	frame.frame_data.previous_view_projection =
	    same_camera ? m_taa_history.previous_view_projection : frame.frame_data.view_projection;
	frame.frame_data.jittered_view_projection = frame.frame_data.view_projection;
	frame.taa_active = taa;

	// R2 offsets so accumulated frames converge
	{
		constexpr double k_r2_x = 0.7548776662466927;
		constexpr double k_r2_y = 0.5698402909980532;
		const auto n = static_cast<double>(frame.sequence % 65536);
		const auto size = static_cast<double>(blue_noise::k_size);
		frame.frame_data.noise_params = glm::vec4(
		    static_cast<float>(std::floor(std::fmod(n * k_r2_x, 1.0) * size)),
		    static_cast<float>(std::floor(std::fmod(n * k_r2_y, 1.0) * size)),
		    static_cast<float>(frame.sequence % 65536),
		    0.0f
		);
	}
	frame.taa_reset = taa && (!m_taa_history.was_active || !same_camera);

	frame.jitter_ndc = glm::vec2(0.0f);
	if (taa && render_extent.width > 0 && render_extent.height > 0) {
		// R2 low discrepancy sequence from the plastic number
		constexpr double k_plastic = 1.32471795724474602596;
		const auto n = static_cast<double>(++m_taa_history.jitter_index);
		const glm::vec2 pixel_offset(
		    static_cast<float>(std::fmod(0.5 + (n / k_plastic), 1.0)) - 0.5f,
		    static_cast<float>(std::fmod(0.5 + (n / (k_plastic * k_plastic)), 1.0)) - 0.5f
		);
		const glm::vec2 ndc_offset =
		    2.0f * pixel_offset / glm::vec2(static_cast<float>(render_extent.width), static_cast<float>(render_extent.height));
		frame.jitter_ndc = ndc_offset;
		frame.frame_data.jittered_view_projection =
		    glm::translate(glm::mat4(1.0f), glm::vec3(ndc_offset, 0.0f)) * frame.frame_data.view_projection;
	}

	if (!is_capture) {
		m_taa_history.previous_view_projection = frame.frame_data.view_projection;
		m_taa_history.camera = m_camera;
		m_taa_history.has_previous = true;
		m_taa_history.was_active = taa;
		m_voxel_previous_models.swap(m_voxel_drawn_models);
	}
}

}
