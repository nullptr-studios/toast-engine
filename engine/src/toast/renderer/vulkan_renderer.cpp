/// @file vulkan_renderer.cpp
/// @author dario
/// @date 16/05/2026

#include "toast/renderer/vulkan_renderer.hpp"

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

VulkanRenderer* VulkanRenderer::instance = nullptr;

auto VulkanRenderer::getPrepassDrawnCount() const noexcept -> uint32_t {
	return m_depth_prepass != nullptr ? m_depth_prepass->getDrawnCount() : 0u;
}

auto VulkanRenderer::getPosedVertexBuffer(uint32_t frame_index) const -> vk::Buffer {
	return m_skinning_pass != nullptr ? m_skinning_pass->getPosedVertexBuffer(frame_index) : vk::Buffer {};
}

auto VulkanRenderer::materialUsesCutout(assets::Material* material) const -> bool {
	if (material == nullptr) {
		return false;
	}

	// No lock since only the render thread mutates m_material_passes
	const auto it = m_material_passes.find(material);
	return it != m_material_passes.end() && it->second->usesCutout();
}

auto VulkanRenderer::getFailsafeTextureView(bool has_reference, const VulkanTexture* texture) const noexcept -> vk::ImageView {
	if (!has_reference) {
		return nullptr;
	}

	if (texture == nullptr) {
		return m_missing_texture.isReady() ? m_missing_texture.getView() : vk::ImageView {};
	}

	switch (texture->state()) {
		case IVulkanResource::UploadState::failed_load:
			return m_fail_load_texture.isReady() ? m_fail_load_texture.getView() : vk::ImageView {};
		case IVulkanResource::UploadState::failed_gpu:
			return m_fail_gpu_texture.isReady() ? m_fail_gpu_texture.getView() : vk::ImageView {};
		default: return m_missing_texture.isReady() ? m_missing_texture.getView() : vk::ImageView {};
	}
}

void VulkanRenderer::requestMaterialFrameSetRebuild() {
	std::lock_guard lock(m_pass_mutex);
	for (auto& [material, pass] : m_material_passes) {
		pass->markShadersDirty();
	}
}

void VulkanRenderer::cancelIrradianceBake() {
	if (m_irradiance_bake_cursor < 0) {
		return;
	}

	TOAST_INFO("Render", "Irradiance volume set changed mid-bake; cancelling so bases can be reassigned");
	m_irradiance_bake_cursor = -1;
}

void VulkanRenderer::beginDebugLineCollection(RenderFrame& frame) {
	std::scoped_lock lock(m_debug_line_mutex);
	frame.debug_line_vertices.insert(
	    frame.debug_line_vertices.end(), m_pending_debug_line_vertices.begin(), m_pending_debug_line_vertices.end()
	);
	m_pending_debug_line_vertices.clear();
	m_collecting_debug_lines = true;
}

void VulkanRenderer::endDebugLineCollection() {
	std::scoped_lock lock(m_debug_line_mutex);
	m_collecting_debug_lines = false;
}

void VulkanRenderer::queueDebugLine(glm::vec3 a, glm::vec3 b, glm::vec4 color) {
	std::scoped_lock lock(m_debug_line_mutex);
	auto& vertices = m_collecting_debug_lines ? beginFrameBuild().debug_line_vertices : m_pending_debug_line_vertices;
	vertices.push_back({a, color});
	vertices.push_back({b, color});
}

void VulkanRenderer::setActiveCamera(toast::Camera* camera) {
	m_camera = camera;
}

void VulkanRenderer::forgetCamera(const toast::Camera* camera) {
	if (m_camera == camera) {
		m_camera = nullptr;
	}
}

auto VulkanRenderer::setPostProcessPassEnabled(std::string_view name, bool enabled) -> bool {
	std::lock_guard lock(m_pass_mutex);
	for (auto& pass : m_post_process_passes) {
		if (pass->name() == name) {
			pass->setEnabled(enabled);
			return true;
		}
	}
	return false;
}

auto VulkanRenderer::isPostProcessPassEnabled(std::string_view name) const -> bool {
	std::lock_guard lock(m_pass_mutex);
	for (const auto& pass : m_post_process_passes) {
		if (pass->name() == name) {
			return pass->isEnabled();
		}
	}
	return false;
}

void VulkanRenderer::addComputePass(std::unique_ptr<IComputePass> pass) {
	m_compute_passes.push_back(std::move(pass));
}

auto VulkanRenderer::applyResize(vk::Extent2D extent) -> void {
	if (extent.width == 0 || extent.height == 0) {
		return;
	}

	m_pending_resize_packed.store(detail::packExtent(extent), std::memory_order_release);
	m_frame_cv.notify_one();
}

void VulkanRenderer::setRenderScale(float scale) {
	scale = std::clamp(scale, 0.5f, 1.0f);
	if (m_render_scale.exchange(scale, std::memory_order_relaxed) != scale) {
		applyResize(m_output_target->getExtent());
	}
}

void VulkanRenderer::requestSettingValue(std::string key, toast::settings::Value value) {
	const std::lock_guard lock(m_setting_request_mutex);
	m_setting_requests.emplace_back(std::move(key), std::move(value));
}

auto VulkanRenderer::renderExtent() const -> vk::Extent2D {
	const vk::Extent2D output = m_output_target->getExtent();
	const float scale = m_applied_render_scale.load(std::memory_order_relaxed);
	return {
	  std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(output.width) * scale))),
	  std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(output.height) * scale))),
	};
}

void VulkanRenderer::stop() {
	ZoneScoped;
	const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
	if (!was_running) {
		return;
	}

	{
		// Locked so the notify cannot slip between the m_running check and the wait
		std::lock_guard lock(m_queue_mutex);
	}
	m_frame_cv.notify_all();

	if (m_render_thread.joinable()) {
		m_render_thread.join();
	}

	while (m_pending_upload_builds.load(std::memory_order_acquire) > 0) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	if (m_core) {
		try {
			m_core->getDevice().waitIdle();
		} catch (const std::exception& e) {
			TOAST_ERROR("Render", "Device wait failed during shutdown, tearing down anyway: {}", e.what());
		}
		for (auto& frame : m_frames) {
			frame.retired.clear();
		}
		m_core->savePipelineCache();
	}

	m_gpu_timer.reset();

#ifdef TRACY_ENABLE
	if (m_tracy_vk_ctx != nullptr) {
		TracyVkDestroy(m_tracy_vk_ctx);
		m_tracy_vk_ctx = nullptr;
	}
#endif
}

VulkanRenderer::VulkanRenderer(const VulkanCore& core, std::unique_ptr<IOutputTarget> output_target) noexcept
    : m_core(&core),
      m_output_target(std::move(output_target)) {
	ZoneScoped;
	instance = this;
	if (!m_output_target) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires an output target!");
	}

	if (k_frames_in_flight == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires at least one frame in flight!");
	}

	TOAST_TRACE("Render", "Creating renderer with {} frame(s) in flight", k_frames_in_flight);
	// Before any pipeline is created
	core.loadPipelineCache();
	m_depth_format = selectDepthFormat(core);

	createGraphicsCommandPool();
	createTransferCommandPool();

	createUploadRing();
	createFrameContexts();
	createPerImageSync();
	createDepthResources();
	createSceneColorResources();
	const auto output_image_count = m_output_target->getImageCount();
	m_images_in_flight.assign(output_image_count, vk::Fence {});
	m_output_image_usage.assign(output_image_count, sync::Usage::none);

	createDescriptorPool();

	createFrameResources();

	createPresentResources();

	m_depth_prepass = std::make_unique<DepthPrepass>(*m_core, m_depth_format, m_output_target->getExtent());

	m_selection_outline = std::make_unique<SelectionOutline>(
	    *m_core, m_output_target->getColorFormat(), m_depth_format, m_output_target->getExtent()
	);

	m_skinning_pass = std::make_unique<SkinningPass>(*m_core);
	m_voxel_trace_scene = std::make_unique<VoxelTraceScene>(*m_core, k_frames_in_flight);

	if (m_core->isRayTracingSupported()) {
		m_ray_tracing_scene = std::make_unique<RayTracingScene>(*m_core, k_frames_in_flight);
		m_skinned_blas_pool = std::make_unique<SkinnedBlasPool>(*m_core);
		m_voxel_blas_cache = std::make_unique<VoxelBlasCache>(*m_core, k_frames_in_flight);
	}

	m_capture_listener.subscribe<event::CaptureFrame>([this](const auto&) {
		m_capture_frame_requested.store(true, std::memory_order_release);
		return true;
	});

#if defined(TOAST_DEV_TOOLS)
	m_imgui_input_listener.subscribe<event::WindowMousePosition>([this](const auto& e) {
		m_imgui_mouse_pos = {e.x, e.y};
		return false;
	});
	m_imgui_input_listener.subscribe<event::WindowMouseButton>([this](const auto& e) {
		int idx = -1;
		switch (e.button) {
			case 1: idx = 0; break;
			case 3: idx = 1; break;
			case 2: idx = 2; break;
			default: break;
		}
		if (idx >= 0) {
			m_imgui_mouse_down[static_cast<size_t>(idx)] = (e.action == 1);
		}
		return false;
	});
	m_imgui_input_listener.subscribe<event::WindowMouseScroll>([this](const auto& e) {
		m_imgui_wheel_x_accum += e.x;
		m_imgui_wheel_y_accum += e.y;
		return false;
	});
	m_imgui_input_listener.subscribe<event::WindowKey>([this](const auto& e) {
		m_imgui_key_events_accum.push_back({e.key, e.action != 0});
		if (e.key == SDLK_F3 && e.action == event::window_input_pressed && m_game_view.load(std::memory_order_relaxed)) {
			toast::settings::Settings::get().setValue(k_debug_overlay_in_game, toast::settings::Value {!debugOverlayInGame()});
		}
		return false;
	});
	m_imgui_input_listener.subscribe<event::WindowChar>([this](const auto& e) {
		m_imgui_char_events_accum.push_back(e.key);
		return false;
	});
#endif

	m_hdr_listener.subscribe<event::WindowHdr>([this](const auto& e) {
		setDisplayHdr(e.sdr_white, e.headroom);
		return false;
	});

	m_render_mode_listener.subscribe<event::SetRenderMode>([this](const auto& e) {
		m_render_mode = e.mode;
		return true;
	});

	createDefaultTexture();

	m_asset_listener.subscribe<event::ClearUnusedAssets>([this] {
		m_pending_material_pass_clear.store(true, std::memory_order_release);
		return false;
	});
	m_asset_listener.subscribe<event::ShaderRecompiled>([this](const event::ShaderRecompiled&) {
		std::lock_guard lock(m_pass_mutex);
		for (auto& [material, pass] : m_material_passes) {
			pass->markShadersDirty();
		}
		return false;
	});
	m_asset_listener.subscribe<event::MaterialAssetReloaded>([this](const event::MaterialAssetReloaded&) {
		std::lock_guard lock(m_pass_mutex);
		for (auto& [material, pass] : m_material_passes) {
			pass->markShadersDirty();
		}
		return false;
	});
}

VulkanRenderer::~VulkanRenderer() {
	stop();

	detail::clearCachedAssetHandles();
	instance = nullptr;
}

void VulkanRenderer::retire(std::shared_ptr<void> object) {
	if (object != nullptr) {
		m_frames[m_current_frame].retired.push_back(std::move(object));
	}
}

auto VulkanRenderer::listPasses() -> std::vector<PassInfo> {
	std::lock_guard lock(m_pass_mutex);
	std::vector<PassInfo> out;
	out.reserve(m_material_passes.size() + m_render_passes.size());
	for (auto& [material, pass] : m_material_passes) {
		out.push_back(PassInfo {.name = std::string(pass->name()), .enabled = pass->isEnabled()});
	}
	for (auto& pass : m_render_passes) {
		out.push_back(PassInfo {.name = std::string(pass->name()), .enabled = pass->isEnabled()});
	}
	for (auto& pass : m_post_process_passes) {
		out.push_back(PassInfo {.name = std::string(pass->name()), .enabled = pass->isEnabled()});
	}
	return out;
}

void VulkanRenderer::setPassEnabled(std::string_view name, bool enabled) {
	std::lock_guard lock(m_pass_mutex);
	for (auto& [material, pass] : m_material_passes) {
		if (pass->name() == name) {
			pass->setEnabled(enabled);
		}
	}
	for (auto& pass : m_render_passes) {
		if (pass->name() == name) {
			pass->setEnabled(enabled);
		}
	}
	for (auto& pass : m_post_process_passes) {
		if (pass->name() == name) {
			pass->setEnabled(enabled);
		}
	}
}

void VulkanRenderer::start() noexcept {
	TOAST_TRACE("Render", "Starting renderer");

	m_running.store(true, std::memory_order_release);

	m_render_thread = std::thread([this] { mainRenderThread(); });
}

void VulkanRenderer::setRenderingPaused(bool paused) noexcept {
	{
		// Under the queue lock so the render thread cannot miss the wake up
		std::lock_guard lock(m_queue_mutex);
		m_rendering_paused.store(paused, std::memory_order_release);
	}
	m_frame_cv.notify_all();
}

void VulkanRenderer::setGameView(bool game_view) {
	m_game_view.store(game_view, std::memory_order_relaxed);
	const bool owns = game_view && debugWindowsVisible() && debugOverlayInGame();
	if (owns != m_debug_overlay_owns_cursor.load(std::memory_order_relaxed)) {
		// Queued first so whatever the editor sends after polling the flag lands behind it
		event::send<event::DebugOverlayCursor>(owns);
		m_debug_overlay_owns_cursor.store(owns, std::memory_order_release);
	}
}

void VulkanRenderer::registerMeshNodeProxy(toast::MeshNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_mesh_proxy_mutex);
	if (!std::ranges::contains(m_mesh_proxy_nodes, node)) {
		m_mesh_proxy_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterMeshNodeProxy(toast::MeshNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_mesh_proxy_mutex);
	std::erase(m_mesh_proxy_nodes, node);
}

void VulkanRenderer::registerVoxelNodeProxy(toast::VoxelNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_voxel_proxy_mutex);
	if (!std::ranges::contains(m_voxel_proxy_nodes, node)) {
		m_voxel_proxy_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterVoxelNodeProxy(toast::VoxelNode* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_voxel_proxy_mutex);
	std::erase(m_voxel_proxy_nodes, node);
}

void VulkanRenderer::registerReflectionProbeProxy(toast::ReflectionProbe* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	if (!std::ranges::contains(m_reflection_probe_nodes, node)) {
		m_reflection_probe_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterReflectionProbeProxy(toast::ReflectionProbe* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	std::erase(m_reflection_probe_nodes, node);
	m_probe_load_attempted.erase(node);
}

void VulkanRenderer::registerIrradianceVolumeProxy(toast::IrradianceVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	if (!std::ranges::contains(m_irradiance_volume_nodes, node)) {
		m_irradiance_volume_nodes.push_back(node);
		cancelIrradianceBake();
	}
}

void VulkanRenderer::unregisterIrradianceVolumeProxy(toast::IrradianceVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	std::erase(m_irradiance_volume_nodes, node);
	m_irradiance_load_attempted.erase(node);
	m_irradiance_baked_keys.erase(node);
	cancelIrradianceBake();
}

void VulkanRenderer::registerPostProcessVolumeProxy(toast::PostProcessVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	if (!std::ranges::contains(m_post_process_volume_nodes, node)) {
		m_post_process_volume_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterPostProcessVolumeProxy(toast::PostProcessVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	std::erase(m_post_process_volume_nodes, node);
}

void VulkanRenderer::registerFogVolumeProxy(toast::FogVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	if (!std::ranges::contains(m_fog_volume_nodes, node)) {
		m_fog_volume_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterFogVolumeProxy(toast::FogVolume* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_reflection_probe_mutex);
	std::erase(m_fog_volume_nodes, node);
}

void VulkanRenderer::registerLightNodeProxy(toast::Light* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_light_proxy_mutex);
	if (!std::ranges::contains(m_light_proxy_nodes, node)) {
		m_light_proxy_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterLightNodeProxy(toast::Light* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_light_proxy_mutex);
	std::erase(m_light_proxy_nodes, node);
}

void VulkanRenderer::registerCameraNodeProxy(toast::Camera* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_camera_proxy_mutex);
	if (!std::ranges::contains(m_camera_proxy_nodes, node)) {
		m_camera_proxy_nodes.push_back(node);
	}
}

void VulkanRenderer::unregisterCameraNodeProxy(toast::Camera* node) {
	if (node == nullptr) {
		return;
	}

	std::scoped_lock lock(m_camera_proxy_mutex);
	std::erase(m_camera_proxy_nodes, node);
}

auto VulkanRenderer::applyResizeInternal(vk::Extent2D extent) -> void {
	ZoneScoped;
	if (!m_output_target->isPresentable()) {
		uint64_t expected = k_no_pending_resize;
		m_pending_resize_packed.compare_exchange_strong(expected, detail::packExtent(extent), std::memory_order_acq_rel);
		return;
	}

	m_core->getDevice().waitIdle();
	publishCompletedFrames();
	m_pending_publish.clear();
	m_applied_render_scale.store(m_render_scale.load(std::memory_order_relaxed), std::memory_order_relaxed);
	for (auto& frame : m_frames) {
		frame.has_submitted = false;
	}
	try {
		m_output_target->recreate(extent);
		const vk::Extent2D actual_extent = m_output_target->getExtent();
		const auto image_count = m_output_target->getImageCount();
		m_images_in_flight.assign(image_count, vk::Fence {});
		m_output_image_usage.assign(image_count, sync::Usage::none);
		createPerImageSync();
		createDepthResources();
		createSceneColorResources();
		if (m_selection_outline != nullptr) {
			m_selection_outline->onResize(actual_extent);
		}

		{
			std::lock_guard lock(m_pass_mutex);
			for (auto& pass : m_post_process_passes) {
				pass->onResize(pass->atOutputResolution() ? actual_extent : renderExtent());
			}
		}
		if (m_voxel_pass != nullptr) {
			m_voxel_pass->onTargetsRecreated();
		}
		std::ranges::fill(m_present_bound_views, vk::ImageView {});
		m_current_frame = 0;
	} catch (const std::exception& e) { TOAST_CRITICAL("Render", "Failed to recreate output target on resize: {}", e.what()); }
}

void VulkanRenderer::addRenderPass(std::unique_ptr<IRenderPass> pass) {
	m_render_passes.push_back(std::move(pass));
}

void VulkanRenderer::addPostProcessPass(std::unique_ptr<IPostProcessPass> pass) {
	std::lock_guard lock(m_pass_mutex);
	m_post_process_passes.push_back(std::move(pass));
}

// DEBUG SHI

void VulkanRenderer::registerDebugDraw(toast::Node3D* node, void (*draw)(toast::Node3D&)) {
	std::scoped_lock lock(m_mesh_proxy_mutex);
	if (std::ranges::none_of(m_debug_nodes, [node](const auto& entry) { return entry.first == node; })) {
		m_debug_nodes.emplace_back(node, draw);
	}
}

void VulkanRenderer::unregisterDebugDraw(toast::Node3D* node) {
	std::scoped_lock lock(m_mesh_proxy_mutex);
	std::erase_if(m_debug_nodes, [node](const auto& entry) { return entry.first == node; });
}

}
