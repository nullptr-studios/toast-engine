/// @file VulkanRenderer.cpp
/// @author dario
/// @date 16/05/2026

#include "vulkan_renderer.hpp"

#include "cube_face_basis.hpp"
#include "passes/depth_prepass.hpp"
#include "passes/environment_pass.hpp"
#include "passes/material_pass.hpp"
#include "passes/reflection_probe_pass.hpp"
#include "passes/shadow_pass.hpp"
#include "passes/skinning_pass.hpp"
#include "ray_tracing_scene.hpp"
#include "skinned_blas_pool.hpp"
#include "vulkan_debug.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <toast/assets/animation.hpp>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>
#include <toast/assets/material.hpp>
#include <toast/log.hpp>
#include <toast/thread_pool.hpp>
#include <toast/time.hpp>
#include <toast/voxel/runtime_pool.hpp>
#include <toast/voxel/volume_bounds.hpp>
#include <toast/window/window_events.hpp>
#include <toast/world/animation_player.hpp>
#include <toast/world/camera.hpp>
#include <toast/world/irradiance_volume.hpp>
#include <toast/world/light.hpp>
#include <toast/world/mesh_node.hpp>
#include <toast/world/point_light.hpp>
#include <toast/world/post_process_volume.hpp>
#include <toast/world/reflection_probe.hpp>
#include <toast/world/spotlight.hpp>
#include <toast/world/voxel_node.hpp>
#include <toast/world/workspace_events.hpp>
#include <tracy/Tracy.hpp>
#include <tuple>

#if defined(_WIN32)
#include <external/inc/nsight/NGFX_GPUTrace_Vulkan.h>
#include <external/inc/nsight/NGFX_GraphicsCapture_Vulkan.h>
#endif

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace renderer {

VulkanRenderer* VulkanRenderer::instance = nullptr;

namespace {

auto depthAttachmentRange(vk::Format format) -> vk::ImageSubresourceRange {
	vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eDepth;
	switch (format) {
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint: aspect |= vk::ImageAspectFlagBits::eStencil; break;
		default: break;
	}
	return {aspect, 0, 1, 0, 1};
}

auto transitionImageLayout(
    vk::CommandBuffer command_buffer, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
    vk::AccessFlags src_access_mask, vk::AccessFlags dst_access_mask, vk::PipelineStageFlags src_stage_mask,
    vk::PipelineStageFlags dst_stage_mask, vk::ImageSubresourceRange subresource_range
) -> void {
	const vk::ImageMemoryBarrier barrier(
	    src_access_mask,
	    dst_access_mask,
	    old_layout,
	    new_layout,
	    VK_QUEUE_FAMILY_IGNORED,
	    VK_QUEUE_FAMILY_IGNORED,
	    image,
	    subresource_range
	);
	command_buffer.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {}, barrier);
}

struct DirectionalCascadeFit {
	glm::mat4 view_projection {1.0f};
	glm::vec4 cull_sphere {0.0f};
	float depth_bias = 0.0f;
	float texel_world_size = 0.0f;
};

constexpr float k_caster_back_off = 100.0f;

auto fitDirectionalCascade(
    const toast::Camera& camera, float aspect, glm::vec3 light_direction, float split_near, float split_far, uint32_t resolution
) -> DirectionalCascadeFit {
	constexpr float k_world_depth_bias = 0.01f;

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
	radius = std::ceil(radius * 16.0f) / 16.0f;
	radius = std::max(radius, 0.001f);

	const glm::vec3 direction = glm::normalize(light_direction);
	const glm::vec3 up =
	    std::abs(glm::dot(direction, toast::Node3D::world_up)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : toast::Node3D::world_up;

	const glm::mat4 light_view = glm::lookAt(center - direction * (radius + k_caster_back_off), center, up);

	const float depth_extent = (2.0f * radius) + k_caster_back_off;

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
	  .cull_sphere = glm::vec4(center, radius + k_caster_back_off),
	  .depth_bias = k_world_depth_bias / depth_extent,
	  .texel_world_size = 2.0f * radius / static_cast<float>(resolution),
	};
}

auto computeCascadeSplits(float near_plane, float far_plane) -> std::array<float, shadows::k_cascade_count> {
	std::array<float, shadows::k_cascade_count> splits {};
	const float range = far_plane - near_plane;

	for (uint32_t i = 0; i < shadows::k_cascade_count; ++i) {
		const float fraction = static_cast<float>(i + 1) / static_cast<float>(shadows::k_cascade_count);
		const float logarithmic = near_plane * std::pow(far_plane / near_plane, fraction);
		const float uniform = near_plane + (range * fraction);
		splits[i] = glm::mix(uniform, logarithmic, shadows::k_cascade_split_lambda);
	}

	return splits;
}

/// @brief Inward normalized frustum planes. With [0,1] clip depth near is row 2 alone
auto extractFrustumPlanes(const glm::mat4& view_projection) -> std::array<glm::vec4, 6> {
	const auto& m = view_projection;
	// glm is column major
	const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
	const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
	const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
	const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

	std::array planes {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};

	for (auto& plane : planes) {
		const float length = glm::length(glm::vec3(plane));
		if (length > 0.0f) {
			plane /= length;
		}
	}
	return planes;
}

auto sphereInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& center, float radius) -> bool {
	for (const auto& plane : planes) {
		if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
			return false;
		}
	}
	return true;
}

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

auto packExtent(vk::Extent2D extent) -> uint64_t {
	return (static_cast<uint64_t>(extent.width) << 32u) | static_cast<uint64_t>(extent.height);
}

auto unpackExtent(uint64_t packed) -> vk::Extent2D {
	return {static_cast<uint32_t>(packed >> 32u), static_cast<uint32_t>(packed & 0xFFFFFFFFu)};
}

assets::Handle<assets::Mesh> g_camera_gizmo_mesh;
bool g_camera_gizmo_resolved = false;

std::array<assets::Handle<assets::Texture>, 5> g_light_icons;
std::array<bool, 5> g_light_icons_resolved {};

void clearCachedAssetHandles() {
	g_camera_gizmo_mesh = {};
	g_camera_gizmo_resolved = false;
	g_light_icons = {};
	g_light_icons_resolved = {};
}

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
    toast::MeshNode& node, renderer::VulkanRenderer::RenderFrame& frame, uint32_t& joint_offset, uint32_t& joint_count,
    bool& pool_exhausted_warned, SkinPoseCache& pose_cache
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

	if (frame.joint_matrices.size() + joint_matrices.size() > renderer::VulkanRenderer::k_max_joint_matrices) {
		if (!pool_exhausted_warned) {
			pool_exhausted_warned = true;
			TOAST_WARN(
			    "Render",
			    "Joint matrix pool ({} matrices/frame) is full; some skinned meshes will draw in bind pose this frame",
			    renderer::VulkanRenderer::k_max_joint_matrices
			);
		}
		return;
	}

	joint_offset = static_cast<uint32_t>(frame.joint_matrices.size());
	joint_count = static_cast<uint32_t>(joint_matrices.size());
	frame.joint_matrices.insert(frame.joint_matrices.end(), joint_matrices.begin(), joint_matrices.end());
	pose_cache.emplace(SkinPoseKey {player, skin}, std::pair {joint_offset, joint_count});
}

void uploadSinglePixelTexture(
    const renderer::VulkanCore& core, renderer::VulkanTexture& texture, std::array<uint8_t, 4> pixel, std::string_view debug_name
) {
	const auto& device = core.getDevice();

	renderer::VulkanTexture::Params params {};
	params.format = vk::Format::eR8G8B8A8Unorm;
	params.extent = vk::Extent3D {1, 1, 1};
	params.mip_levels = 1;
	params.layer_count = 1;
	texture.create(core, params, debug_name);

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = pixel.size();
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	auto staging_buffer = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	setDebugName(core, *staging_buffer, std::format("{} StagingBuffer", debug_name));
	std::memcpy(staging_buffer.getAllocation().getInfo().pMappedData, pixel.data(), pixel.size());

	vk::raii::CommandPool one_shot_pool(device, vk::CommandPoolCreateInfo({}, core.getGraphicsQueueFamilyIndex()));
	const vk::CommandBufferAllocateInfo cmd_alloc_info(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1);
	auto cmd_buffers = device.allocateCommandBuffers(cmd_alloc_info);
	vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	recordUndefinedToTransferDst(cmd, texture.getImage(), colorSubresourceRange());

	vk::BufferImageCopy region {};
	region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	region.imageExtent = vk::Extent3D {1, 1, 1};
	cmd.copyBufferToImage(*staging_buffer, texture.getImage(), vk::ImageLayout::eTransferDstOptimal, region);

	recordTransferDstToShaderRead(cmd, texture.getImage(), colorSubresourceRange());

	cmd.end();

	submitAndWait(device, core.getGraphicsQueue(), *cmd);

	texture.markReady();
}

}

auto VulkanRenderer::selectDepthFormat(const VulkanCore& core) -> vk::Format {
	const std::array candidates {
	  vk::Format::eD32Sfloat, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint, vk::Format::eD16Unorm
	};

	for (const auto candidate : candidates) {
		const auto props = core.getPhysicalDevice().getFormatProperties(candidate);
		if ((props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlags {}) {
			return candidate;
		}
	}

	TOAST_CRITICAL("Render", "Toast Engine Error: Failed to find a supported depth format!");
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
	m_depth_format = selectDepthFormat(core);

	createGraphicsCommandPool();
	createTransferCommandPool();
	createComputeCommandPool();

	createUploadRing();
	createFrameContexts();
	createPerImageSync();
	createDepthResources();
	createSceneColorResources();
	const auto output_image_count = m_output_target->getImageCount();
	m_images_in_flight.assign(output_image_count, vk::Fence {});
	m_output_image_layouts.assign(output_image_count, vk::ImageLayout::eUndefined);

	createDescriptorPool();

	createFrameResources();

	createPresentResources();

	m_depth_prepass = std::make_unique<DepthPrepass>(*m_core, m_depth_format, m_output_target->getExtent());

	m_skinning_pass = std::make_unique<SkinningPass>(*m_core);

	if (m_core->isRayTracingSupported()) {
		m_ray_tracing_scene = std::make_unique<RayTracingScene>(*m_core, k_frames_in_flight);
		m_skinned_blas_pool = std::make_unique<SkinnedBlasPool>(*m_core);
	}

	m_capture_listener.subscribe<event::CaptureFrame>([this](const auto&) {
		m_capture_frame_requested.store(true, std::memory_order_release);
		return true;
	});

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
		return false;
	});
	m_imgui_input_listener.subscribe<event::WindowChar>([this](const auto& e) {
		m_imgui_char_events_accum.push_back(e.key);
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

	clearCachedAssetHandles();
	instance = nullptr;
}

auto VulkanRenderer::createGraphicsCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getGraphicsQueueFamilyIndex()
	);
	m_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_command_pool, "VulkanRenderer GraphicsCommandPool");
	TOAST_TRACE("Render", "Graphics command pool created (graphics family {})", m_core->getGraphicsQueueFamilyIndex());
}

auto VulkanRenderer::createTransferCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getTransferQueueFamilyIndex()
	);
	m_transfer_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_transfer_command_pool, "VulkanRenderer TransferCommandPool");
	TOAST_TRACE("Render", "Transfer command pool created (transfer family {})", m_core->getTransferQueueFamilyIndex());
}

auto VulkanRenderer::createComputeCommandPool() -> void {
	const vk::CommandPoolCreateInfo pool_ci(
	    vk::CommandPoolCreateFlagBits::eResetCommandBuffer, m_core->getComputeQueueFamilyIndex()
	);
	m_compute_command_pool = vk::raii::CommandPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_compute_command_pool, "VulkanRenderer ComputeCommandPool");
	TOAST_TRACE("VulkanRenderer", "Compute command pool created (compute family {})", m_core->getComputeQueueFamilyIndex());
}

auto VulkanRenderer::createUploadRing() -> void {
	m_upload_slots.clear();
	m_upload_slots.resize(k_upload_slots);
	m_next_upload_slot = 0;

	const vk::CommandBufferAllocateInfo allocate_ci(*m_transfer_command_pool, vk::CommandBufferLevel::ePrimary, k_upload_slots);
	auto buffers = m_core->getDevice().allocateCommandBuffers(allocate_ci);

	for (uint32_t i = 0; i < k_upload_slots; ++i) {
		m_upload_slots[i].command_buffer = std::move(buffers[i]);
		m_upload_slots[i].fence = vk::raii::Fence(m_core->getDevice(), vk::FenceCreateInfo {});
		setDebugName(*m_core, *m_upload_slots[i].command_buffer, std::format("VulkanRenderer Upload[{}] CommandBuffer", i));
		setDebugName(*m_core, *m_upload_slots[i].fence, std::format("VulkanRenderer Upload[{}] Fence", i));
	}

	TOAST_TRACE("VulkanRenderer", "Upload command buffer ring created: {} slots", k_upload_slots);
}

auto VulkanRenderer::createFrameContexts() -> void {
	ZoneScoped;
	m_frames.clear();
	m_frames.resize(k_frames_in_flight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_command_pool, vk::CommandBufferLevel::ePrimary, k_frames_in_flight);
	const vk::CommandBufferAllocateInfo compute_command_buffer_ci(
	    *m_compute_command_pool, vk::CommandBufferLevel::ePrimary, k_frames_in_flight
	);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);
	auto allocated_compute_command_buffers = m_core->getDevice().allocateCommandBuffers(compute_command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < k_frames_in_flight; ++frame_index) {
		m_frames[frame_index].command_buffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].compute_command_buffer = std::move(allocated_compute_command_buffers[frame_index]);
		m_frames[frame_index].image_available = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].compute_to_graphics = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].in_flight = vk::raii::Fence(m_core->getDevice(), fence_ci);
		m_frames[frame_index].compute_in_flight = vk::raii::Fence(m_core->getDevice(), fence_ci);

		setDebugName(
		    *m_core, *m_frames[frame_index].command_buffer, std::format("VulkanRenderer Frame[{}] CommandBuffer", frame_index)
		);
		setDebugName(
		    *m_core,
		    *m_frames[frame_index].compute_command_buffer,
		    std::format("VulkanRenderer Frame[{}] ComputeCommandBuffer", frame_index)
		);
		setDebugName(
		    *m_core, *m_frames[frame_index].image_available, std::format("VulkanRenderer Frame[{}] ImageAvailable", frame_index)
		);
		setDebugName(
		    *m_core,
		    *m_frames[frame_index].compute_to_graphics,
		    std::format("VulkanRenderer Frame[{}] ComputeToGraphics", frame_index)
		);
		setDebugName(*m_core, *m_frames[frame_index].in_flight, std::format("VulkanRenderer Frame[{}] InFlightFence", frame_index));
		setDebugName(
		    *m_core,
		    *m_frames[frame_index].compute_in_flight,
		    std::format("VulkanRenderer Frame[{}] ComputeInFlightFence", frame_index)
		);
	}

	TOAST_TRACE("Render", "Frame command buffers created: {}", k_frames_in_flight);
}

auto VulkanRenderer::createPerImageSync() -> void {
	const auto image_count = m_output_target->getImageCount();
	m_render_finished_per_image.clear();
	m_render_finished_per_image.reserve(image_count);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	for (uint32_t i = 0; i < image_count; ++i) {
		m_render_finished_per_image.emplace_back(m_core->getDevice(), semaphore_ci);
		setDebugName(*m_core, *m_render_finished_per_image.back(), std::format("VulkanRenderer RenderFinished[{}]", i));
	}

	TOAST_TRACE("Render", "Per-image semaphores created: {}", image_count);
}

auto VulkanRenderer::createDepthResources() -> void {
	ZoneScoped;
	if (m_depth_format == vk::Format::eUndefined) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires a valid depth format!");
	}

	const auto extent = m_output_target->getExtent();
	if (extent.width == 0 || extent.height == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires a non-zero output extent for depth resources!");
	}

	m_depth_resources.view.reset();
	m_depth_resources.image.reset();

	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_depth_format;
	image_ci.extent = vk::Extent3D {extent.width, extent.height, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	auto depth_image = m_core->getAllocator().createImage(image_ci, allocation_ci);
	m_depth_resources.image.emplace(std::move(depth_image));
	setDebugName(*m_core, **m_depth_resources.image, "VulkanRenderer DepthImage");

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_depth_resources.image;
	view_ci.viewType = vk::ImageViewType::e2D;
	view_ci.format = m_depth_format;
	view_ci.subresourceRange = depthAttachmentRange(m_depth_format);
	m_depth_resources.view.emplace(m_core->getDevice(), view_ci);
	setDebugName(*m_core, **m_depth_resources.view, "VulkanRenderer DepthImageView");

	m_depth_layout = vk::ImageLayout::eUndefined;
	TOAST_TRACE(
	    "Render", "Depth resources created at {}x{} with format {}", extent.width, extent.height, vk::to_string(m_depth_format)
	);
}

void VulkanRenderer::createSceneColorResources() {
	ZoneScoped;
	const auto extent = m_output_target->getExtent();
	if (extent.width == 0 || extent.height == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires a non-zero output extent for the scene target!");
	}

	const auto create = [&](SceneColorResources& target, vk::Format format, std::string_view label) {
		target.view.reset();
		target.image.reset();

		const auto image_ci = colorTargetImageInfo(extent, format);

		vma::AllocationCreateInfo allocation_ci {};
		allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

		target.image.emplace(m_core->getAllocator().createImage(image_ci, allocation_ci));
		setDebugName(*m_core, **target.image, std::format("VulkanRenderer {}Image", label));

		vk::ImageViewCreateInfo view_ci {};
		view_ci.image = **target.image;
		view_ci.viewType = vk::ImageViewType::e2D;
		view_ci.format = format;
		view_ci.subresourceRange = colorSubresourceRange();
		target.view.emplace(m_core->getDevice(), view_ci);
		setDebugName(*m_core, **target.view, std::format("VulkanRenderer {}View", label));

		target.layout = vk::ImageLayout::eUndefined;
		TOAST_TRACE("Render", "{} target created at {}x{} with format {}", label, extent.width, extent.height, vk::to_string(format));
	};

	create(m_scene_color, m_scene_color_format, "SceneColor");
	create(m_scene_normal, m_scene_normal_format, "SceneNormal");
	create(m_scene_indirect, m_scene_indirect_format, "SceneIndirect");
}

void VulkanRenderer::createDescriptorPool() {
	std::vector<vk::DescriptorPoolSize> pool_sizes {
	  vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8192),

	  vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 32768),

	  vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 8192)
	};

	if (m_core->isRayTracingSupported()) {
		pool_sizes.emplace_back(vk::DescriptorType::eAccelerationStructureKHR, 2048);
	}
	vk::DescriptorPoolCreateInfo pool_ci {};
	pool_ci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

	pool_ci.maxSets = 16384;

	pool_ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());

	pool_ci.pPoolSizes = pool_sizes.data();

	m_descriptor_pool = vk::raii::DescriptorPool(m_core->getDevice(), pool_ci);
	setDebugName(*m_core, *m_descriptor_pool, "VulkanRenderer DescriptorPool");
}

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

void VulkanRenderer::publishCompletedFrames() {
	ZoneScoped;
	while (!m_pending_publish.empty()) {
		const uint32_t index = m_pending_publish.front();
		auto& pending = m_frames[index];

		if (!pending.has_submitted) {
			m_pending_publish.pop_front();
			continue;
		}

		if (pending.in_flight.getStatus() != vk::Result::eSuccess) {
			break;
		}

		if (!pending.was_probe_capture) {
			m_output_target->onImageRenderComplete(pending.last_image_index);
		}

		pending.has_submitted = false;
		m_pending_publish.pop_front();
	}
}

namespace {

auto isTraceable(const VulkanRenderer::MeshInstanceProxy& proxy) -> bool {
	if (proxy.mesh == nullptr) {
		return false;
	}

	if (proxy.posed_vertex_offset != VulkanRenderer::MeshInstanceProxy::k_no_posed_vertices) {
		return proxy.mesh->isReady() && proxy.mesh->getVertexCount() > 0 && proxy.mesh->getIndexCount() >= 3;
	}

	return proxy.mesh->hasAccelerationStructure();
}

}

void VulkanRenderer::recordAccelerationStructureBuilds(FrameContext& frame) {
	ZoneScoped;
	if (!m_core->isRayTracingSupported()) {
		return;
	}

	const auto* render_frame = renderingFrame();
	if (render_frame == nullptr) {
		return;
	}

	constexpr uint32_t k_max_builds_per_frame = 8;
	uint32_t built = 0;

	std::unordered_set<VulkanMesh*> seen;
	for (const auto& proxy : render_frame->mesh_instances) {
		if (built >= k_max_builds_per_frame) {
			break;
		}
		if (proxy.mesh == nullptr || !proxy.mesh->isReady() || proxy.mesh->hasAccelerationStructure()) {
			continue;
		}
		if (!seen.insert(proxy.mesh).second) {
			continue;
		}

		auto scratch = proxy.mesh->createAccelerationStructure(*m_core);
		if (!scratch.has_value()) {
			continue;
		}

		proxy.mesh->recordBuildAccelerationStructure(*frame.command_buffer);
		frame.blas_scratch.push_back(std::move(*scratch));
		++built;
	}

	const bool tracing = render_frame->frame_data.traced_shadow_params.x >= 0.5f;

	uint32_t skinned_recorded = 0;
	if (m_skinned_blas_pool != nullptr && tracing) {
		m_skinned_blas_pool->beginFrame();

		const vk::Buffer posed = getPosedVertexBuffer(m_current_frame);
		for (const auto& proxy : render_frame->mesh_instances) {
			if (proxy.mesh == nullptr || !proxy.mesh->isReady() ||
			    proxy.posed_vertex_offset == MeshInstanceProxy::k_no_posed_vertices) {
				continue;
			}
			if (m_skinned_blas_pool->recordFor(*frame.command_buffer, proxy.node_uid, *proxy.mesh, posed, proxy.posed_vertex_offset) !=
			    0) {
				++skinned_recorded;
			}
		}

		m_skinned_blas_pool->endFrame();
	}

	if (built > 0 || skinned_recorded > 0) {
		vk::MemoryBarrier barrier {};
		barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
		barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;

		frame.command_buffer.pipelineBarrier(
		    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
		    vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits::eFragmentShader,
		    {},
		    barrier,
		    nullptr,
		    nullptr
		);
	}

	if (m_ray_tracing_scene != nullptr) {
		m_ray_tracing_scene->beginFrame();
		for (const auto& proxy : render_frame->mesh_instances) {
			if (!isTraceable(proxy)) {
				continue;
			}

			const bool posed = proxy.posed_vertex_offset != MeshInstanceProxy::k_no_posed_vertices;
			vk::DeviceAddress blas_address = 0;
			if (!posed) {
				blas_address = proxy.mesh->getAccelerationStructureAddress();
			} else if (m_skinned_blas_pool != nullptr) {
				blas_address = m_skinned_blas_pool->addressFor(proxy.node_uid);
			}
			if (blas_address == 0) {
				continue;
			}

			m_ray_tracing_scene->addInstance(
			    {.transform = posed ? glm::mat4(1.0f) : proxy.model, .blas_address = blas_address, .deforming = posed}
			);
		}
		m_ray_tracing_scene->build(*frame.command_buffer, m_current_frame, tracing);
	}
}

void VulkanRenderer::recordMeshScene(vk::CommandBuffer cmd, uint32_t image_index) {
	// Opaque first since m_material_passes is unordered
	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Opaque materials");
		for (auto& [material, pass] : m_material_passes) {
			if (!pass->isEnabled() || pass->isBlended()) {
				continue;
			}
			TracyVkZone(m_tracy_vk_ctx, cmd, "MaterialPass");
			pass->record(cmd, m_current_frame, image_index);
		}
	}

	{
		const auto* render_frame = renderingFrame();
		if (render_frame != nullptr) {
			struct BlendedDraw {
				float distance_squared = 0.0f;
				MaterialPass* pass = nullptr;
				const MeshInstanceProxy* proxy = nullptr;
			};

			std::vector<BlendedDraw> draws;
			const glm::vec3 camera_position = render_frame->frame_data.camera_position;

			for (auto& [material, pass] : m_material_passes) {
				if (!pass->isEnabled() || !pass->isBlended()) {
					continue;
				}
				for (const auto& proxy : render_frame->mesh_instances) {
					if (proxy.root_material != pass->rootMaterial() || !proxy.visible) {
						continue;
					}
					const glm::vec3 delta = glm::vec3(proxy.model[3]) - camera_position;
					draws.push_back({glm::dot(delta, delta), pass.get(), &proxy});
				}
			}

			std::ranges::sort(draws, [](const BlendedDraw& a, const BlendedDraw& b) {
				if (a.distance_squared != b.distance_squared) {
					return a.distance_squared > b.distance_squared;
				}
				return a.pass < b.pass;
			});

			TracyVkZone(m_tracy_vk_ctx, cmd, "BlendedDraws");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, cmd, "Blended materials");
			for (const auto& draw : draws) {
				draw.pass->recordInstance(cmd, m_current_frame, *draw.proxy);
			}
		}
	}
}

auto VulkanRenderer::recordFrame(FrameContext& frame, uint32_t image_index) noexcept -> void {
	ZoneScoped;
	frame.command_buffer.reset();
	const vk::CommandBufferBeginInfo begin_info(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.command_buffer.begin(begin_info);
	if (m_gpu_timer != nullptr) {
		m_gpu_timer->beginGraphics(m_current_frame, *frame.command_buffer);
	}

	// Before the AS builds which read its output
	if (m_skinning_pass != nullptr) {
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Skinning");
		m_skinning_pass->record(*frame.command_buffer, m_current_frame);
	}

	frame.blas_scratch.clear();
	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Acceleration structures");
		recordAccelerationStructureBuilds(frame);
	}

	{
		const GpuScope offscreen_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Off-screen");
		for (auto& pass : m_render_passes) {
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			pass->recordPre(*frame.command_buffer, m_current_frame, image_index);
		}
	}

	const auto extent = m_output_target->getExtent();
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);

	const auto* capture_source = renderingFrame();
	const uint32_t capture_size = capture_source != nullptr ? capture_source->capture_extent : 0u;
	const vk::Extent2D scene_extent = capture_size > 0 ? vk::Extent2D {capture_size, capture_size} : extent;
	const vk::Viewport scene_viewport(
	    0.0f, 0.0f, static_cast<float>(scene_extent.width), static_cast<float>(scene_extent.height), 0.0f, 1.0f
	);
	const vk::Rect2D scene_scissor({0, 0}, scene_extent);

	const vk::Image scene_image = m_scene_color.image ? **m_scene_color.image : VK_NULL_HANDLE;
	if (scene_image != VK_NULL_HANDLE) {
		const bool was_sampled = m_scene_color.layout == vk::ImageLayout::eShaderReadOnlyOptimal;
		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    m_scene_color.layout,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    was_sampled ? vk::AccessFlagBits::eShaderRead : vk::AccessFlags {},
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    was_sampled ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    colorSubresourceRange()
		);
		m_scene_color.layout = vk::ImageLayout::eColorAttachmentOptimal;
	}

	const vk::Image normal_image = m_scene_normal.image ? **m_scene_normal.image : VK_NULL_HANDLE;
	if (normal_image != VK_NULL_HANDLE && m_scene_normal.layout != vk::ImageLayout::eColorAttachmentOptimal) {
		const bool normal_was_sampled = m_scene_normal.layout == vk::ImageLayout::eShaderReadOnlyOptimal;
		transitionImageLayout(
		    frame.command_buffer,
		    normal_image,
		    m_scene_normal.layout,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    normal_was_sampled ? vk::AccessFlagBits::eShaderRead : vk::AccessFlags {},
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    normal_was_sampled ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    colorSubresourceRange()
		);
		m_scene_normal.layout = vk::ImageLayout::eColorAttachmentOptimal;
	}

	const vk::Image indirect_image = m_scene_indirect.image ? **m_scene_indirect.image : VK_NULL_HANDLE;
	if (indirect_image != VK_NULL_HANDLE && m_scene_indirect.layout != vk::ImageLayout::eColorAttachmentOptimal) {
		const bool indirect_was_sampled = m_scene_indirect.layout == vk::ImageLayout::eShaderReadOnlyOptimal;
		transitionImageLayout(
		    frame.command_buffer,
		    indirect_image,
		    m_scene_indirect.layout,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    indirect_was_sampled ? vk::AccessFlagBits::eShaderRead : vk::AccessFlags {},
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    indirect_was_sampled ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    colorSubresourceRange()
		);
		m_scene_indirect.layout = vk::ImageLayout::eColorAttachmentOptimal;
	}

	const vk::Image depth_image = m_depth_resources.image ? **m_depth_resources.image : VK_NULL_HANDLE;
	if (depth_image != VK_NULL_HANDLE && m_depth_layout != vk::ImageLayout::eDepthAttachmentOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    depth_image,
		    m_depth_layout,
		    vk::ImageLayout::eDepthAttachmentOptimal,
		    vk::AccessFlags {},
		    vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		    vk::PipelineStageFlagBits::eTopOfPipe,
		    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
		    depthAttachmentRange(m_depth_format)
		);
		m_depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;
	}

	const vk::ClearValue clear_color(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 1.0f}));
	const vk::ClearValue clear_depth(vk::ClearDepthStencilValue {1.0f, 0});

	vk::RenderingAttachmentInfo scene_attachment_info {};
	scene_attachment_info.imageView = scene_image != VK_NULL_HANDLE ? **m_scene_color.view : nullptr;
	scene_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	scene_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	scene_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	scene_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	scene_attachment_info.clearValue = clear_color;

	const vk::ClearValue clear_normal(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	vk::RenderingAttachmentInfo normal_attachment_info {};
	normal_attachment_info.imageView = normal_image != VK_NULL_HANDLE ? **m_scene_normal.view : nullptr;
	normal_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	normal_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	normal_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	normal_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	normal_attachment_info.clearValue = clear_normal;

	const bool prepass_active = m_depth_prepass != nullptr && m_depth_prepass->isReady() && m_depth_resources.view.has_value();

	if (prepass_active) {
		vk::RenderingAttachmentInfo prepass_depth {};
		prepass_depth.imageView = **m_depth_resources.view;
		prepass_depth.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		prepass_depth.loadOp = vk::AttachmentLoadOp::eClear;
		prepass_depth.storeOp = vk::AttachmentStoreOp::eStore;
		prepass_depth.clearValue = clear_depth;

		vk::RenderingInfo prepass_info {};
		prepass_info.renderArea = scene_scissor;
		prepass_info.layerCount = 1;
		prepass_info.colorAttachmentCount = 0;
		prepass_info.pDepthAttachment = &prepass_depth;

		frame.command_buffer.beginRendering(prepass_info);
		frame.command_buffer.setViewport(0, std::array {scene_viewport});
		frame.command_buffer.setScissor(0, std::array {scene_scissor});
		{
			TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "DepthPrepass");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Depth prepass");
			m_depth_prepass->record(*frame.command_buffer, m_current_frame);
		}
		frame.command_buffer.endRendering();
	}

	vk::RenderingAttachmentInfo depth_attachment_info {};
	if (m_depth_resources.view.has_value()) {
		depth_attachment_info.imageView = **m_depth_resources.view;
		depth_attachment_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		depth_attachment_info.loadOp = prepass_active ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
		depth_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
		depth_attachment_info.clearValue = clear_depth;
	}

	vk::RenderingAttachmentInfo indirect_attachment_info {};
	indirect_attachment_info.imageView = indirect_image != VK_NULL_HANDLE ? **m_scene_indirect.view : nullptr;
	indirect_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	indirect_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	indirect_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	indirect_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	indirect_attachment_info.clearValue = vk::ClearValue(vk::ClearColorValue(std::array {0.0f, 0.0f, 0.0f, 0.0f}));

	const std::array scene_color_attachments {scene_attachment_info, normal_attachment_info, indirect_attachment_info};

	vk::RenderingInfo scene_rendering_info {};
	scene_rendering_info.renderArea = scene_scissor;
	scene_rendering_info.layerCount = 1;
	scene_rendering_info.colorAttachmentCount = static_cast<uint32_t>(scene_color_attachments.size());
	scene_rendering_info.pColorAttachments = scene_color_attachments.data();
	if (m_depth_resources.view.has_value()) {
		scene_rendering_info.pDepthAttachment = &depth_attachment_info;
	}
	frame.command_buffer.beginRendering(scene_rendering_info);
	frame.command_buffer.setViewport(0, std::array {scene_viewport});
	frame.command_buffer.setScissor(0, std::array {scene_scissor});

	{
		TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "Scene");
		const GpuScope scene_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Scene");
		std::lock_guard lock(m_pass_mutex);

		recordMeshScene(*frame.command_buffer, image_index);

		for (auto& pass : m_render_passes) {
			if (!pass->isEnabled() || pass->stage() != RenderStage::world) {
				continue;
			}
			TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "WorldPass");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			pass->record(*frame.command_buffer, m_current_frame, image_index);
		}
	}

	frame.command_buffer.endRendering();

	if (const auto* capture_frame = renderingFrame(); capture_frame != nullptr && capture_frame->probe_capture_index >= 0 &&
	                                                  m_reflection_probe_pass != nullptr && scene_image != VK_NULL_HANDLE) {
		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::AccessFlagBits::eTransferRead,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    vk::PipelineStageFlagBits::eTransfer,
		    colorSubresourceRange()
		);

		m_reflection_probe_pass->captureFace(
		    *frame.command_buffer,
		    scene_image,
		    scene_extent,
		    static_cast<uint32_t>(capture_frame->probe_capture_index),
		    capture_frame->probe_capture_face
		);

		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::AccessFlagBits::eTransferRead,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    colorSubresourceRange()
		);
	}

	if (const auto* capture_frame = renderingFrame(); capture_frame != nullptr && capture_frame->irradiance_capture_index >= 0 &&
	                                                  m_reflection_probe_pass != nullptr && scene_image != VK_NULL_HANDLE) {
		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::AccessFlagBits::eTransferRead,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    vk::PipelineStageFlagBits::eTransfer,
		    colorSubresourceRange()
		);

		m_reflection_probe_pass->captureIrradianceFace(
		    *frame.command_buffer, scene_image, scene_extent, capture_frame->irradiance_capture_face
		);

		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::AccessFlagBits::eTransferRead,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    colorSubresourceRange()
		);

		if (capture_frame->irradiance_capture_face == 5) {
			m_reflection_probe_pass->projectStagingToSh(
			    *frame.command_buffer, static_cast<uint32_t>(capture_frame->irradiance_capture_index)
			);
		}
	}

	if (scene_image != VK_NULL_HANDLE) {
		transitionImageLayout(
		    frame.command_buffer,
		    scene_image,
		    vk::ImageLayout::eColorAttachmentOptimal,
		    vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    colorSubresourceRange()
		);
		m_scene_color.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	}

	if (normal_image != VK_NULL_HANDLE && m_scene_normal.layout != vk::ImageLayout::eShaderReadOnlyOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    normal_image,
		    m_scene_normal.layout,
		    vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    colorSubresourceRange()
		);
		m_scene_normal.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	}

	if (indirect_image != VK_NULL_HANDLE && m_scene_indirect.layout != vk::ImageLayout::eShaderReadOnlyOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    indirect_image,
		    m_scene_indirect.layout,
		    vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::AccessFlagBits::eColorAttachmentWrite,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eColorAttachmentOutput,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    colorSubresourceRange()
		);
		m_scene_indirect.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
	}

	if (depth_image != VK_NULL_HANDLE && m_depth_layout != vk::ImageLayout::eDepthReadOnlyOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    depth_image,
		    m_depth_layout,
		    vk::ImageLayout::eDepthReadOnlyOptimal,
		    vk::AccessFlagBits::eDepthStencilAttachmentWrite,
		    vk::AccessFlagBits::eShaderRead,
		    vk::PipelineStageFlagBits::eLateFragmentTests,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    depthAttachmentRange(m_depth_format)
		);
		m_depth_layout = vk::ImageLayout::eDepthReadOnlyOptimal;
	}

	const auto* capture_check = renderingFrame();
	const bool capture_frame =
	    capture_check != nullptr && (capture_check->probe_capture_index >= 0 || capture_check->irradiance_capture_index >= 0);

	vk::ImageView post_source = getSceneColorView();
	if (!capture_frame) {
		std::lock_guard lock(m_pass_mutex);
		const GpuScope post_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Post process");
		for (auto& pass : m_post_process_passes) {
			if (!pass->isEnabled() || !post_source) {
				continue;
			}
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			post_source = pass->record(*frame.command_buffer, m_current_frame, post_source);
		}
	}

	if (depth_image != VK_NULL_HANDLE && m_depth_layout != vk::ImageLayout::eDepthAttachmentOptimal) {
		transitionImageLayout(
		    frame.command_buffer,
		    depth_image,
		    m_depth_layout,
		    vk::ImageLayout::eDepthAttachmentOptimal,
		    vk::AccessFlagBits::eShaderRead,
		    vk::AccessFlagBits::eDepthStencilAttachmentRead,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    vk::PipelineStageFlagBits::eEarlyFragmentTests,
		    depthAttachmentRange(m_depth_format)
		);
		m_depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;
	}

	const vk::Image image = m_output_target->getColorImage(image_index);
	const vk::ImageLayout previous_layout = m_output_image_layouts.at(image_index);

	vk::AccessFlags src_access_mask {};
	vk::PipelineStageFlags src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;

	switch (previous_layout) {
		case vk::ImageLayout::eUndefined:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
			break;
		case vk::ImageLayout::ePresentSrcKHR:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eBottomOfPipe;
			break;
		case vk::ImageLayout::eTransferSrcOptimal:
			src_access_mask = vk::AccessFlagBits::eTransferRead;
			src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
			break;
		case vk::ImageLayout::eColorAttachmentOptimal:
			src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
			src_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			break;
		default:
			src_access_mask = vk::AccessFlags {};
			src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
			break;
	}

	transitionImageLayout(
	    frame.command_buffer,
	    image,
	    previous_layout,
	    vk::ImageLayout::eColorAttachmentOptimal,
	    src_access_mask,
	    vk::AccessFlagBits::eColorAttachmentWrite,
	    src_stage_mask,
	    vk::PipelineStageFlagBits::eColorAttachmentOutput,
	    colorSubresourceRange()
	);

	vk::RenderingAttachmentInfo output_attachment_info {};
	output_attachment_info.imageView = *m_output_target->getColorAttachment(image_index);
	output_attachment_info.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	output_attachment_info.resolveMode = vk::ResolveModeFlagBits::eNone;
	output_attachment_info.loadOp = vk::AttachmentLoadOp::eClear;
	output_attachment_info.storeOp = vk::AttachmentStoreOp::eStore;
	output_attachment_info.clearValue = clear_color;

	vk::RenderingAttachmentInfo overlay_depth_info {};
	if (m_depth_resources.view.has_value()) {
		overlay_depth_info.imageView = **m_depth_resources.view;
		overlay_depth_info.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
		overlay_depth_info.loadOp = vk::AttachmentLoadOp::eLoad;
		overlay_depth_info.storeOp = vk::AttachmentStoreOp::eDontCare;
	}

	vk::RenderingInfo output_rendering_info {};
	output_rendering_info.renderArea = scissor;
	output_rendering_info.layerCount = 1;
	output_rendering_info.colorAttachmentCount = 1;
	output_rendering_info.pColorAttachments = &output_attachment_info;
	if (m_depth_resources.view.has_value()) {
		output_rendering_info.pDepthAttachment = &overlay_depth_info;
	}
	frame.command_buffer.beginRendering(output_rendering_info);
	frame.command_buffer.setViewport(0, std::array {viewport});
	frame.command_buffer.setScissor(0, std::array {scissor});

	{
		TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "Present");
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Present blit");

		if (m_present_pipeline.isReady() && post_source && m_current_frame < m_present_sets.size()) {
			if (m_present_bound_views[m_current_frame] != post_source) {
				vk::DescriptorImageInfo image_info {};
				image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				image_info.imageView = post_source;
				image_info.sampler = *m_present_sampler;

				const vk::WriteDescriptorSet write(
				    *m_present_sets[m_current_frame], 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info
				);
				m_core->getDevice().updateDescriptorSets(write, {});
				m_present_bound_views[m_current_frame] = post_source;
			}

			frame.command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_present_pipeline.getPipeline());
			frame.command_buffer.bindDescriptorSets(
			    vk::PipelineBindPoint::eGraphics,
			    *m_present_layout.getPipelineLayout(),
			    0,
			    std::array<vk::DescriptorSet, 1> {*m_present_sets[m_current_frame]},
			    {}
			);
			frame.command_buffer.draw(3, 1, 0, 0);
		}
	}

	if (!capture_frame) {
		std::lock_guard lock(m_pass_mutex);
		const GpuScope overlay_scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Overlays");
		for (auto& pass : m_render_passes) {
			if (!pass->isEnabled() || pass->stage() != RenderStage::overlay) {
				continue;
			}
			TracyVkZone(m_tracy_vk_ctx, *frame.command_buffer, "OverlayPass");
			const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, pass->name());
			pass->record(*frame.command_buffer, m_current_frame, image_index);
		}
	}

	frame.command_buffer.endRendering();

	{
		const GpuScope scope(m_gpu_timer.get(), m_current_frame, *frame.command_buffer, "Output finalize");
		m_output_target->recordFinalize(frame.command_buffer, image_index);
	}
	m_output_image_layouts.at(image_index) =
	    m_output_target->usesAcquirePresentSemaphores() ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eTransferSrcOptimal;

	TracyVkCollect(m_tracy_vk_ctx, *frame.command_buffer);

	if (m_gpu_timer != nullptr) {
		m_gpu_timer->endGraphics(m_current_frame, *frame.command_buffer);
	}
	frame.command_buffer.end();
}

void VulkanRenderer::createPresentResources() {
	ZoneScoped;
	const auto uid = assets::resolveURI("core://shaders/present.slang");
	const auto shader = uid.has_value() ? ShaderCache::get().acquire(*uid) : nullptr;
	if (!shader) {
		TOAST_ERROR("Render", "core://shaders/present.slang unavailable; nothing will reach the screen");
		return;
	}

	m_present_layout.rebuild(*m_core, shader->reflection, "Present");

	VulkanPipeline::Config config;
	config.pipeline_type = VulkanPipeline::PipelineType::graphics;
	config.debug_name = "Present";
	config.color_format = m_output_target->getColorFormat();
	config.extent = m_output_target->getExtent();
	config.shader_spirv = shader->spirv;
	config.pipeline_layout = *m_present_layout.getPipelineLayout();
	config.vertex_bindings = {};
	config.vertex_attributes = {};
	config.cull_mode = vk::CullModeFlagBits::eNone;
	config.depth_format = m_depth_format;
	config.depth_test = false;
	config.depth_write = false;
	m_present_pipeline.rebuild(*m_core, config);

	const auto& device = m_core->getDevice();

	const auto sampler_ci = linearClampSamplerInfo();
	m_present_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(*m_core, *m_present_sampler, "VulkanRenderer PresentSampler");

	const auto& layouts = m_present_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	m_present_sets.clear();
	m_present_bound_views.assign(k_frames_in_flight, vk::ImageView {});
	for (uint32_t i = 0; i < k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(*m_descriptor_pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_present_sets.push_back(std::move(allocated[0]));
		setDebugName(*m_core, *m_present_sets[i], std::format("VulkanRenderer PresentSet[{}]", i));
	}
}

void VulkanRenderer::createFrameResources() {
	ZoneScoped;
	m_frame_ubo_res.resize(k_frames_in_flight);
	m_frame_ubos.resize(k_frames_in_flight);

	const auto& device = m_core->getDevice();

	const vk::DeviceSize buffer_size = sizeof(FrameUBO);

	for (uint32_t i = 0; i < m_frame_ubo_res.size(); ++i) {
		auto& frame = m_frame_ubo_res[i];

		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = buffer_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;

		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		frame.gpu_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, **frame.gpu_buffer, std::format("VulkanRenderer FrameUBO[{}]", i));
	}

	m_joint_matrix_res.resize(k_frames_in_flight);
	const vk::DeviceSize joint_matrix_buffer_size = sizeof(glm::mat4) * k_max_joint_matrices;
	for (uint32_t i = 0; i < m_joint_matrix_res.size(); ++i) {
		auto& frame = m_joint_matrix_res[i];

		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = joint_matrix_buffer_size;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		frame.gpu_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, **frame.gpu_buffer, std::format("VulkanRenderer JointMatrices[{}]", i));
	}

	const auto create_instance_buffers = [this](std::vector<FrameResources>& target, std::string_view debug_name) {
		target.resize(k_frames_in_flight);
		for (uint32_t i = 0; i < target.size(); ++i) {
			vk::BufferCreateInfo buffer_ci {};
			buffer_ci.size = sizeof(InstanceData) * k_max_instances;
			buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

			vma::AllocationCreateInfo alloc_ci {};
			alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
			alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

			target[i].gpu_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
			setDebugName(*m_core, **target[i].gpu_buffer, std::format("VulkanRenderer {}[{}]", debug_name, i));
		}
	};

	create_instance_buffers(m_instance_res, "Instances");
	create_instance_buffers(m_shadow_instance_res, "ShadowInstances");
}

void VulkanRenderer::createDefaultTexture() {
	const auto& device = m_core->getDevice();

	uploadSinglePixelTexture(*m_core, m_default_texture, {255, 255, 255, 255}, "VulkanRenderer DefaultWhiteTexture");
	uploadSinglePixelTexture(*m_core, m_default_black_texture, {0, 0, 0, 255}, "VulkanRenderer DefaultBlackTexture");
	uploadSinglePixelTexture(*m_core, m_default_normal_texture, {128, 128, 255, 255}, "VulkanRenderer DefaultNormalTexture");

	createDefaultShadowMap();
	createDefaultCubemap();

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eRepeat;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	m_default_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(*m_core, *m_default_sampler, "VulkanRenderer DefaultSampler");

	createFailsafeTextures();
}

void VulkanRenderer::createFailsafeTextures() {
	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eRepeat;
	sampler_ci.maxLod = VK_LOD_CLAMP_NONE;
	m_failsafe_sampler = vk::raii::Sampler(m_core->getDevice(), sampler_ci);
	setDebugName(*m_core, *m_failsafe_sampler, "VulkanRenderer FailsafeSampler");

	const auto load = [this](VulkanTexture& target, std::string_view virtual_path, std::string_view debug_name) {
		auto bytes = assets::AssetManager::get().tryLoadBytes(virtual_path);
		if (!bytes.has_value()) {
			TOAST_WARN("Render", "Failsafe texture '{}' is missing; that slot will fall back to flat white", virtual_path);
			return;
		}
		uploadTextureSync(*m_core, target, std::move(*bytes), debug_name);
	};

	load(m_fail_load_texture, "core://textures/fail_load.ktx2", "VulkanRenderer FailLoadTexture");
	load(m_fail_gpu_texture, "core://textures/fail_gpu.ktx2", "VulkanRenderer FailGpuTexture");
	load(m_missing_texture, "core://textures/MissingTexture.ktx2", "VulkanRenderer MissingTexture");
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

void VulkanRenderer::createDefaultShadowMap() {
	ZoneScoped;
	const auto& device = m_core->getDevice();

	// Comparison samplers only read depth images
	vk::ImageCreateInfo image_ci {};
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = m_depth_format;
	image_ci.extent = vk::Extent3D {1, 1, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 1;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_default_shadow_image.emplace(m_core->getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(*m_core, **m_default_shadow_image, "VulkanRenderer DefaultShadowImage");

	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);

	const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
	const vk::raii::CommandPool one_shot_pool(device, pool_ci);
	const vk::CommandBufferAllocateInfo cmd_alloc_info(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1);
	auto cmd_buffers = device.allocateCommandBuffers(cmd_alloc_info);
	const vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	recordUndefinedToTransferDst(cmd, **m_default_shadow_image, range);

	const vk::ClearDepthStencilValue clear_value(1.0f, 0);
	cmd.clearDepthStencilImage(**m_default_shadow_image, vk::ImageLayout::eTransferDstOptimal, clear_value, range);

	recordTransferDstToShaderRead(cmd, **m_default_shadow_image, range);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), *cmd);

	// Sampler2DArrayShadow needs an array view
	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_default_shadow_image;
	view_ci.viewType = vk::ImageViewType::e2DArray;
	view_ci.format = m_depth_format;
	view_ci.subresourceRange = range;
	m_default_shadow_view = vk::raii::ImageView(device, view_ci);
	setDebugName(*m_core, *m_default_shadow_view, "VulkanRenderer DefaultShadowArrayView");

	m_default_shadow_sampler = createShadowSampler(*m_core, m_depth_format);
	setDebugName(*m_core, *m_default_shadow_sampler, "VulkanRenderer DefaultShadowSampler");
}

void VulkanRenderer::createDefaultCubemap() {
	ZoneScoped;
	const auto& device = m_core->getDevice();

	vk::ImageCreateInfo image_ci {};
	image_ci.flags = vk::ImageCreateFlagBits::eCubeCompatible;
	image_ci.imageType = vk::ImageType::e2D;
	image_ci.format = vk::Format::eR16G16B16A16Sfloat;
	image_ci.extent = vk::Extent3D {1, 1, 1};
	image_ci.mipLevels = 1;
	image_ci.arrayLayers = 6;
	image_ci.samples = vk::SampleCountFlagBits::e1;
	image_ci.tiling = vk::ImageTiling::eOptimal;
	image_ci.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;
	m_default_cube_image.emplace(m_core->getAllocator().createImage(image_ci, allocation_ci));
	setDebugName(*m_core, **m_default_cube_image, "VulkanRenderer DefaultCubemap");

	const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);

	const vk::CommandPoolCreateInfo pool_ci(vk::CommandPoolCreateFlagBits::eTransient, m_core->getGraphicsQueueFamilyIndex());
	const vk::raii::CommandPool one_shot_pool(device, pool_ci);
	const vk::CommandBufferAllocateInfo cmd_alloc_info(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1);
	auto cmd_buffers = device.allocateCommandBuffers(cmd_alloc_info);
	const vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	recordUndefinedToTransferDst(cmd, **m_default_cube_image, range);

	const vk::ClearColorValue clear_value(std::array {0.0f, 0.0f, 0.0f, 1.0f});
	cmd.clearColorImage(**m_default_cube_image, vk::ImageLayout::eTransferDstOptimal, clear_value, range);

	recordTransferDstToShaderRead(cmd, **m_default_cube_image, range);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), *cmd);

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_default_cube_image;
	view_ci.viewType = vk::ImageViewType::eCube;
	view_ci.format = vk::Format::eR16G16B16A16Sfloat;
	view_ci.subresourceRange = range;
	m_default_cube_view = vk::raii::ImageView(device, view_ci);
	setDebugName(*m_core, *m_default_cube_view, "VulkanRenderer DefaultCubemapView");
}

void VulkanRenderer::ensureMaterialPasses(RenderFrame& frame_data) {
	ZoneScoped;

	if (m_pending_material_pass_clear.exchange(false, std::memory_order_acq_rel)) {
		m_core->getDevice().waitIdle();
		std::lock_guard lock(m_pass_mutex);
		m_material_passes.clear();
	}

	std::lock_guard lock(m_pass_mutex);
	for (const auto& proxy : frame_data.mesh_instances) {
		if (proxy.root_material == nullptr || m_material_passes.contains(proxy.root_material)) {
			continue;
		}
		auto pass = std::make_unique<MaterialPass>(
		    *m_core, proxy.root_material, m_scene_color_format, m_depth_format, m_output_target->getExtent()
		);
		TOAST_INFO("Render", "Created material pass '{}'", pass->name());
		m_material_passes.emplace(proxy.root_material, std::move(pass));
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

void VulkanRenderer::updateFrameResources(uint32_t frame_index, RenderFrame& frame_data) {
	ZoneScoped;
	m_frame_ubos[frame_index] = frame_data.frame_data;

	const auto& allocation = m_frame_ubo_res[frame_index].gpu_buffer->getAllocation();
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &m_frame_ubos[frame_index], sizeof(FrameUBO));

		allocation.flush(0, sizeof(FrameUBO));
	}

	if (frame_index < m_joint_matrix_res.size() && m_joint_matrix_res[frame_index].gpu_buffer.has_value() &&
	    !frame_data.joint_matrices.empty()) {
		const auto count = std::min<size_t>(frame_data.joint_matrices.size(), k_max_joint_matrices);
		const auto bytes = static_cast<vk::DeviceSize>(count) * sizeof(glm::mat4);

		const auto& joint_allocation = m_joint_matrix_res[frame_index].gpu_buffer->getAllocation();
		if (auto* joint_mapped = joint_allocation.getInfo().pMappedData) {
			std::memcpy(joint_mapped, frame_data.joint_matrices.data(), bytes);
			joint_allocation.flush(0, bytes);
		}
	}

	const auto upload_instances =
	    [](const std::vector<FrameResources>& target, uint32_t index, const std::vector<InstanceData>& src) {
		    if (index >= target.size() || !target[index].gpu_buffer.has_value() || src.empty()) {
			    return;
		    }
		    const auto count = std::min<size_t>(src.size(), k_max_instances);
		    const auto bytes = static_cast<vk::DeviceSize>(count) * sizeof(InstanceData);

		    const auto& allocation = target[index].gpu_buffer->getAllocation();
		    if (auto* mapped = allocation.getInfo().pMappedData) {
			    std::memcpy(mapped, src.data(), bytes);
			    allocation.flush(0, bytes);
		    }
	    };

	upload_instances(m_instance_res, frame_index, frame_data.instance_data);
	upload_instances(m_shadow_instance_res, frame_index, frame_data.shadow_instance_data);
}

auto VulkanRenderer::drawFrame(RenderFrame& frame_data) -> void {
	ZoneScoped;
	if (m_frames.empty()) {
		return;
	}

	if (!m_output_target->isPresentable()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		return;
	}

	using clock = std::chrono::steady_clock;
	auto phase_start = clock::now();
	auto end_phase = [&phase_start](std::atomic<uint64_t>& counter) {
		const auto now = clock::now();
		counter.fetch_add(
		    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - phase_start).count()),
		    std::memory_order_relaxed
		);
		phase_start = now;
	};

	processPendingUploads();

	flushResourceUploads();
	end_phase(m_perf_upload_ns);

	auto& frame = m_frames[m_current_frame];
	std::ignore = m_core->getDevice().waitForFences(*frame.in_flight, true, std::numeric_limits<uint64_t>::max());
	std::ignore = m_core->getDevice().waitForFences(*frame.compute_in_flight, true, std::numeric_limits<uint64_t>::max());
	end_phase(m_perf_gpu_wait_ns);
	publishCompletedFrames();

	if (m_gpu_timer != nullptr && !frame.was_probe_capture) {
		m_gpu_timer->collect(m_current_frame);
	}
	end_phase(m_perf_record_ns);

	const auto acquired =
	    m_output_target->acquireNextImage(std::numeric_limits<uint64_t>::max(), *frame.image_available, VK_NULL_HANDLE);
	end_phase(m_perf_acquire_ns);

	if (acquired.result == vk::Result::eErrorOutOfDateKHR) {
		TOAST_WARN("Render", "Swapchain out of date on acquire; recreating");
		applyResize(m_output_target->getExtent());
		return;
	}
	if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Failed to acquire the next output image!");
	}

	const uint32_t image_index = acquired.value;
	if (m_images_in_flight.at(image_index)) {
		std::ignore =
		    m_core->getDevice().waitForFences(m_images_in_flight.at(image_index), true, std::numeric_limits<uint64_t>::max());
	}
	end_phase(m_perf_gpu_wait_ns);

	m_core->getDevice().resetFences(*frame.in_flight);
	m_images_in_flight[image_index] = *frame.in_flight;

	updateFrameResources(m_current_frame, frame_data);    // FIXME dt

	m_rendering_frame = &frame_data;
	ensureMaterialPasses(frame_data);

	// TODO move outside the render loop
	{
		std::lock_guard lock(m_pass_mutex);
		for (auto& [material, pass] : m_material_passes) {
			pass->update(m_current_frame, Time::renderDelta());
		}
		for (auto& pass : m_render_passes) {
			pass->update(m_current_frame, Time::renderDelta());
		}
	}

	if (const auto* rendering = renderingFrame(); rendering != nullptr) {
		frame.was_probe_capture = rendering->probe_capture_index >= 0 || rendering->irradiance_capture_index >= 0;
	} else {
		frame.was_probe_capture = false;
	}

	recordFrame(frame, image_index);

	// Never conditional since the graphics submit waits on its semaphore
	m_core->getDevice().resetFences(*frame.compute_in_flight);

	const vk::CommandBuffer compute_command_buffer = *frame.compute_command_buffer;
	compute_command_buffer.reset();
	constexpr vk::CommandBufferBeginInfo compute_begin_info {};
	compute_command_buffer.begin(compute_begin_info);
	if (m_gpu_timer != nullptr) {
		m_gpu_timer->beginCompute(m_current_frame, compute_command_buffer);
	}
	for (auto& pass : m_compute_passes) {
		pass->update(m_current_frame, Time::renderDelta());
		pass->dispatch(compute_command_buffer, m_current_frame);
	}
	if (m_gpu_timer != nullptr) {
		m_gpu_timer->endCompute(m_current_frame, compute_command_buffer);
	}
	compute_command_buffer.end();
	end_phase(m_perf_record_ns);

	const vk::Semaphore compute_to_graphics_semaphore = *frame.compute_to_graphics;
	const vk::SubmitInfo compute_submit_info(0, nullptr, nullptr, 1, &compute_command_buffer, 1, &compute_to_graphics_semaphore);
	m_core->getComputeQueue().submit(compute_submit_info, *frame.compute_in_flight);

	const bool present_sync = m_output_target->usesAcquirePresentSemaphores();

	const vk::CommandBuffer command_buffer = *frame.command_buffer;
	const vk::Semaphore signal_semaphore = *m_render_finished_per_image.at(image_index);
	const vk::Semaphore wait_semaphore = *frame.image_available;

	std::array<vk::Semaphore, 2> wait_semaphores {compute_to_graphics_semaphore, wait_semaphore};
	std::array<vk::PipelineStageFlags, 2> wait_stages {
	  vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eColorAttachmentOutput
	};
	const uint32_t wait_semaphore_count = present_sync ? 2 : 1;

	vk::SubmitInfo submit_info {};
	submit_info.waitSemaphoreCount = wait_semaphore_count;
	submit_info.pWaitSemaphores = wait_semaphores.data();
	submit_info.pWaitDstStageMask = wait_stages.data();
	if (present_sync) {
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &signal_semaphore;
	}
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	vk::FrameBoundaryEXT frame_boundary {};
	if (m_core->isFrameBoundarySupported()) {
		frame_boundary.flags = vk::FrameBoundaryFlagBitsEXT::eFrameEnd;
		frame_boundary.frameID = m_frame_boundary_counter++;
		submit_info.pNext = &frame_boundary;
	}

	{
		std::scoped_lock submit_lock(m_core->graphicsSubmitMutex());
		m_core->getGraphicsQueue().submit(submit_info, *frame.in_flight);
	}
	end_phase(m_perf_submit_ns);

#if defined(_WIN32)
	if (m_core->getNsightMode() == NsightMode::graphics_capture) {
		NGFX_ResourceDescription_Vulkan output_resource {NGFX_ResourceDescription_Vulkan_VER};
		output_resource.type = NGFX_ResourceType_Vulkan_VkImage;
		output_resource.image = static_cast<VkImage>(m_output_target->getColorImage(image_index));

		NGFX_FrameBoundary_Vulkan_Params boundary_params {NGFX_FrameBoundary_Vulkan_Params_VER};
		boundary_params.queue = static_cast<VkQueue>(m_core->getGraphicsQueue());
		boundary_params.outputResources = &output_resource;
		boundary_params.numOutputResources = 1;
		NGFX_FrameBoundary_Vulkan(&boundary_params);
	}
#endif

	const auto present_result = m_output_target->present(image_index, present_sync ? signal_semaphore : vk::Semaphore {});
	end_phase(m_perf_present_ns);
	frame.last_image_index = image_index;
	frame.has_submitted = true;
	m_pending_publish.push_back(m_current_frame);

	m_current_frame = (m_current_frame + 1) % static_cast<uint32_t>(m_frames.size());

	if (present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR) {
		TOAST_WARN("Render", "Swapchain out of date or suboptimal on present; recreating");
		m_rendering_frame = nullptr;
		applyResize(m_output_target->getExtent());
		return;
	}
	if (present_result != vk::Result::eSuccess) {
		TOAST_CRITICAL("Render", "Toast Engine Error: Failed to present the current output image!");
	}

	m_rendering_frame = nullptr;
}

void VulkanRenderer::mainRenderThread() {
	tracy::SetThreadName("Renderer Thread");

#ifdef TRACY_ENABLE
	{
		const vk::CommandBufferAllocateInfo alloc_info(*m_command_pool, vk::CommandBufferLevel::ePrimary, 1);
		const auto tracy_cmd_buffers = m_core->getDevice().allocateCommandBuffers(alloc_info);
		m_tracy_vk_ctx =
		    TracyVkContext(*m_core->getPhysicalDevice(), *m_core->getDevice(), m_core->getGraphicsQueue(), *tracy_cmd_buffers[0]);
	}
#endif

	if (m_core != nullptr) {
		try {
			m_gpu_timer = std::make_unique<GpuTimer>(*m_core, static_cast<uint32_t>(m_frames.size()));
		} catch (const std::exception& e) {
			TOAST_WARN("Render", "GPU timing unavailable, the performance overlay shows CPU timings only: {}", e.what());
			m_gpu_timer.reset();
		}
	}

	using clock = std::chrono::steady_clock;
	auto next_frame_deadline = clock::now();

	RenderFrame frame_to_draw;
	bool has_frame = false;

	std::array<float, 60> frame_time_window {};
	size_t frame_time_cursor = 0;
	clock::time_point last_frame_time {};

	while (m_running.load(std::memory_order_acquire)) {
		ZoneScopedN("VulkanRenderer::mainRenderThread");

		if (m_rendering_paused.load(std::memory_order_acquire)) {
			std::unique_lock lock(m_queue_mutex);
			m_frame_cv.wait(lock, [this] {
				return !m_rendering_paused.load(std::memory_order_acquire) || !m_running.load(std::memory_order_acquire);
			});
			continue;
		}

		const uint64_t pending_resize = m_pending_resize_packed.exchange(k_no_pending_resize, std::memory_order_acq_rel);
		if (pending_resize != k_no_pending_resize) {
			const auto extent = unpackExtent(pending_resize);
			if (extent.width > 0 && extent.height > 0) {
				applyResizeInternal(extent);
			}
		}

		bool consumed_queued_frame = false;

		const double limit_hz = m_bake_active.load(std::memory_order_relaxed) ? 0.0 : effectiveFrameRateLimit();

		{
			std::unique_lock lock(m_queue_mutex);

			auto wake_condition = [this] {
				return !m_ready_frames.empty() || !m_running ||
				       m_pending_resize_packed.load(std::memory_order_acquire) != k_no_pending_resize;
			};

			const auto frame_wait_start = clock::now();
			if (m_ready_frames.empty()) {
				if (!has_frame) {
					m_frame_cv.wait(lock, wake_condition);
				} else if (limit_hz > 0.0) {
					const auto now = clock::now();
					if (next_frame_deadline > now) {
						m_frame_cv.wait_for(lock, next_frame_deadline - now, wake_condition);
					}
				}
			}
			m_perf_frame_wait_ns.fetch_add(
			    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - frame_wait_start).count()),
			    std::memory_order_relaxed
			);

			if (!m_running) {
				return;
			}

			if (!m_ready_frames.empty()) {
				const auto frame_index = m_ready_frames.front();
				m_ready_frames.pop();

				std::swap(frame_to_draw, m_render_frames[frame_index]);
				has_frame = true;
				consumed_queued_frame = true;

				const uint64_t previous = m_last_drawn_sequence.exchange(frame_to_draw.sequence, std::memory_order_relaxed);
				if (frame_to_draw.sequence < previous) {
					m_out_of_order_frames.fetch_add(1, std::memory_order_relaxed);
					TOAST_WARN(
					    "VulkanRenderer",
					    "Frame {} drawn after frame {} - render frames arrived out of order",
					    frame_to_draw.sequence,
					    previous
					);
				} else if (previous != 0 && frame_to_draw.sequence > previous + 1) {
					m_dropped_frames.fetch_add(static_cast<uint32_t>(frame_to_draw.sequence - previous - 1), std::memory_order_relaxed);
				}
			} else if (!has_frame) {
				continue;
			}
		}

		if (limit_hz > 0.0) {
			const auto pacing_start = clock::now();
			std::this_thread::sleep_until(next_frame_deadline);
			m_perf_pacing_ns.fetch_add(
			    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - pacing_start).count()),
			    std::memory_order_relaxed
			);
			const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / limit_hz));
			const auto now = clock::now();
			next_frame_deadline = (next_frame_deadline + interval > now) ? next_frame_deadline + interval : now + interval;
		}

		publishCompletedFrames();

		Time::get().renderTick();

		{
			static constexpr size_t k_window = 60;
			const auto now = clock::now();
			if (last_frame_time.time_since_epoch().count() != 0) {
				const auto interval_ms = std::chrono::duration<float, std::milli>(now - last_frame_time).count();
				frame_time_window[frame_time_cursor % k_window] = interval_ms;
				++frame_time_cursor;

				const size_t filled = std::min<size_t>(frame_time_cursor, k_window);
				float sum = 0.0f;
				float lowest = std::numeric_limits<float>::max();
				float highest = 0.0f;
				for (size_t i = 0; i < filled; ++i) {
					sum += frame_time_window[i];
					lowest = std::min(lowest, frame_time_window[i]);
					highest = std::max(highest, frame_time_window[i]);
				}
				m_frame_time_avg_ms.store(sum / static_cast<float>(filled), std::memory_order_relaxed);
				m_frame_time_min_ms.store(lowest, std::memory_order_relaxed);
				m_frame_time_max_ms.store(highest, std::memory_order_relaxed);
			}
			last_frame_time = now;
		}

		const bool do_capture = m_capture_frame_requested.exchange(false, std::memory_order_acq_rel);
		const auto* rdoc_api = m_core->getRenderDocAPI();
		const auto nsight_mode = m_core->getNsightMode();

		if (do_capture && rdoc_api != nullptr) {
			rdoc_api->StartFrameCapture(nullptr, nullptr);
		}
#if defined(_WIN32)
		else if (do_capture && nsight_mode == NsightMode::graphics_capture) {
			NGFX_GraphicsCapture_StartCapture_Vulkan_Params params {NGFX_GraphicsCapture_StartCapture_Vulkan_Params_VER};
			const NGFX_Result start_result = NGFX_GraphicsCapture_StartCapture_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GraphicsCapture_StartCapture_Vulkan result={}", static_cast<int>(start_result));
		} else if (do_capture && nsight_mode == NsightMode::gpu_trace) {
			m_core->activateNsightGpuTraceIfNeeded();
			NGFX_GPUTrace_StartTrace_Vulkan_Params params {NGFX_GPUTrace_StartTrace_Vulkan_Params_VER};
			const NGFX_Result start_result = NGFX_GPUTrace_StartTrace_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GPUTrace_StartTrace_Vulkan result={}", static_cast<int>(start_result));
		} else if (do_capture) {
			TOAST_WARN(
			    "VulkanRenderer",
			    "Capture requested but no graphics debugger is attached - RenderDoc was not found in this process and "
			    "no Nsight activity initialized (see the VulkanCore log lines at startup)"
			);
		}
#endif

		const auto draw_start = clock::now();
		drawFrame(frame_to_draw);
		m_perf_draw_work_ns.fetch_add(
		    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - draw_start).count()),
		    std::memory_order_relaxed
		);
		m_perf_draws.fetch_add(1, std::memory_order_relaxed);
		if (!consumed_queued_frame) {
			m_perf_repeated_draws.fetch_add(1, std::memory_order_relaxed);
		}

		if (do_capture && rdoc_api != nullptr) {
			rdoc_api->EndFrameCapture(nullptr, nullptr);
			TOAST_INFO("VulkanRenderer", "RenderDoc frame capture triggered");
		}
#if defined(_WIN32)
		else if (do_capture && nsight_mode == NsightMode::graphics_capture) {
			NGFX_GraphicsCapture_StopCapture_Vulkan_Params params {NGFX_GraphicsCapture_StopCapture_Vulkan_Params_VER};
			const NGFX_Result stop_result = NGFX_GraphicsCapture_StopCapture_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GraphicsCapture_StopCapture_Vulkan result={}", static_cast<int>(stop_result));
		} else if (do_capture && nsight_mode == NsightMode::gpu_trace) {
			NGFX_GPUTrace_StopTrace_Vulkan_Params params {NGFX_GPUTrace_StopTrace_Vulkan_Params_VER};
			params.flags = NGFX_GPUTrace_StopTraceFlag_None;
			params.queue = m_core->getGraphicsQueue();
			params.outputResources = nullptr;
			params.numOutputResources = 0;
			const NGFX_Result stop_result = NGFX_GPUTrace_StopTrace_Vulkan(&params);
			TOAST_INFO("VulkanRenderer", "NGFX_GPUTrace_StopTrace_Vulkan result={}", static_cast<int>(stop_result));
		}
#endif

		if (consumed_queued_frame) {
			m_free_frames.release();
		}

#ifdef TRACY_ENABLE
		{
			const auto memory_props = m_core->getPhysicalDevice().getMemoryProperties();
			const auto budgets = m_core->getAllocator().getHeapBudgets();
			uint64_t vram_used = 0;
			uint64_t vram_budget = 0;
			for (uint32_t i = 0; i < memory_props.memoryHeapCount && i < budgets.size(); ++i) {
				if (memory_props.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
					vram_used += budgets[i].usage;
					vram_budget += budgets[i].budget;
				}
			}
			TracyPlot("VRAM used", static_cast<int64_t>(vram_used));
			TracyPlot("VRAM budget", static_cast<int64_t>(vram_budget));
		}
#endif

		FrameMarkNamed("RenderFrame");
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

void VulkanRenderer::submitFrame() noexcept {
	{
		std::lock_guard lock(m_queue_mutex);

		m_ready_frames.push(m_write_index);

		m_write_index = (m_write_index + 1) % k_render_frames;
	}

	m_frame_cv.notify_one();
}

void VulkanRenderer::fitShadowViews(
    RenderFrame& frame, float aspect, int32_t shadow_caster_index, const glm::vec3& shadow_caster_direction,
    std::vector<PunctualShadowCandidate>& spot_candidates, std::vector<PunctualShadowCandidate>& point_candidates
) {
	ZoneScoped;
	if (m_shadow_pass == nullptr) {
		return;
	}
	frame.frame_data.shadow_params.z = 1.0f / static_cast<float>(shadows::cascadeResolution());
	frame.frame_data.shadow_params.w = 1.0f / static_cast<float>(shadows::punctualResolution());

	if (shadow_caster_index >= 0) {
		const float shadow_far = std::min(m_camera->far_plane, shadows::shadowDistance());
		const auto splits = computeCascadeSplits(m_camera->near_plane, shadow_far);

		float split_near = m_camera->near_plane;
		for (uint32_t cascade = 0; cascade < shadows::k_cascade_count; ++cascade) {
			const auto fit = fitDirectionalCascade(
			    *m_camera, aspect, shadow_caster_direction, split_near, splits[cascade], shadows::cascadeResolution()
			);

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

		frame.frame_data.shadow_params.x = static_cast<float>(shadow_caster_index + 1);
		frame.frame_data.shadow_params.y = static_cast<float>(shadows::k_cascade_count);
	}

	const auto by_distance = [](const PunctualShadowCandidate& a, const PunctualShadowCandidate& b) {
		return a.distance_squared < b.distance_squared;
	};
	std::ranges::sort(spot_candidates, by_distance);
	std::ranges::sort(point_candidates, by_distance);

	const size_t spot_shadow_count = std::min<size_t>(spot_candidates.size(), shadows::k_max_spot_shadows);
	for (size_t slot = 0; slot < spot_shadow_count; ++slot) {
		GpuLight& light = frame.lights[spot_candidates[slot].light_index];

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

		const uint32_t resolution = shadows::punctualShadowResolution(
		    std::sqrt(spot_candidates[slot].distance_squared), spot_candidates[slot].resolution_scale
		);

		const uint32_t layer = shadows::k_spot_layer_base + static_cast<uint32_t>(slot);
		light.direction_pad.w = static_cast<float>(layer);
		light.cone_angles.z = shadows::k_punctual_near;
		light.cone_angles.w = range;
		light.shadow_atlas.x = static_cast<float>(resolution) / static_cast<float>(shadows::punctualResolution());
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

	const size_t point_shadow_count = std::min<size_t>(point_candidates.size(), shadows::k_max_point_shadows);
	for (size_t slot = 0; slot < point_shadow_count; ++slot) {
		GpuLight& light = frame.lights[point_candidates[slot].light_index];

		const glm::vec3 position = glm::vec3(light.world_pos_range);
		const float range = std::max(light.world_pos_range.w, 0.01f);

		const uint32_t resolution = shadows::punctualShadowResolution(
		    std::sqrt(point_candidates[slot].distance_squared), point_candidates[slot].resolution_scale
		);

		const uint32_t base_layer = shadows::k_point_layer_base + (static_cast<uint32_t>(slot) * shadows::k_cube_faces);
		light.direction_pad.w = static_cast<float>(base_layer);
		light.cone_angles.z = shadows::k_punctual_near;
		light.cone_angles.w = range;
		light.shadow_atlas.x = static_cast<float>(resolution) / static_cast<float>(shadows::punctualResolution());

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

void VulkanRenderer::tick(float time) noexcept {
	ZoneScopedN("VulkanRenderer::tick()");

	if (m_rendering_paused.load(std::memory_order_relaxed)) {
		beginFrameBuild().debug_line_vertices.clear();
		return;
	}

	using namespace std::chrono_literals;
	const auto slot_wait_start = std::chrono::steady_clock::now();
	const bool got_slot = m_free_frames.try_acquire_for(50ms);

	m_perf_slot_wait_ns.fetch_add(
	    static_cast<uint64_t>(
	        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - slot_wait_start).count()
	    ),
	    std::memory_order_relaxed
	);
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

	auto& frame = beginFrameBuild();

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

	if (!m_camera) {
		if (m_ui_frame_builder) {
			m_ui_frame_builder(frame);
		}
		frame.ui_world_panels.clear();
		count_build();
		submitFrame();
		return;
	}

	m_camera->syncTransform();
	frame.post_process = blendPostProcessVolumes(m_camera->world_position);

	auto& mesh_nodes_snapshot = m_tick_mesh_nodes;
	{
		std::scoped_lock lock(m_mesh_proxy_mutex);
		mesh_nodes_snapshot.assign(m_mesh_proxy_nodes.begin(), m_mesh_proxy_nodes.end());
	}
	frame.mesh_instances.reserve(mesh_nodes_snapshot.size());

	SkinPoseCache skin_pose_cache;

	auto& light_nodes_snapshot = m_tick_light_nodes;
	{
		std::scoped_lock lock(m_light_proxy_mutex);
		light_nodes_snapshot.assign(m_light_proxy_nodes.begin(), m_light_proxy_nodes.end());
	}
	frame.lights.clear();

	for (auto* node : mesh_nodes_snapshot) {
		if (node == nullptr || !node->enabled() || !node->participatesIn(toast::NodeOwnerParticipation::render)) {
			continue;
		}
		if (m_render_owner_filter != nullptr && node->owner() != m_render_owner_filter) {
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

		const auto world_transform = node->worldTransformForRender();

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
		      .node_uid = node->uid().data(),
		    }
		);
	}

	buildVoxelProxies(frame);

	const auto extent = m_output_target->getExtent();
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

	const glm::mat4 camera_view = m_camera->getView();
	const glm::mat4 camera_projection = m_camera->getProjection(aspect);

	frame.frame_data = FrameUBO {
	  .view = camera_view,
	  .projection = camera_projection,
	  .view_projection = camera_projection * camera_view,
	  .camera_position = m_camera->world_position,
	  .time = time,
	  .render_mode_pad = glm::uvec4(frame.render_mode, 0, 0, 0),
	};

	{
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
				    glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, m_camera->near_plane, m_camera->far_plane);

				frame.frame_data.view = capture_view;
				frame.frame_data.projection = capture_projection;
				frame.frame_data.view_projection = capture_projection * capture_view;
				frame.frame_data.camera_position = probe_position;
				frame.probe_capture_index = probe_index;
				frame.probe_capture_face = face;
				frame.capture_extent = std::min({m_reflection_probe_nodes[probe_index]->resolution(), extent.width, extent.height});

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

				debugDrawBox(min_corner, min_corner + extents * 2.0f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));

				const glm::vec3 step = (extents * 2.0f) / glm::max(glm::vec3(counts) - 1.0f, glm::vec3(1.0f));

				const uint32_t total = counts.x * counts.y * counts.z;
				const uint32_t stride = total > 512 ? (total / 512) + 1 : 1;

				const float dot_radius = std::min({step.x, step.y, step.z}) * 0.08f;
				for (uint32_t i = 0; i < total; i += stride) {
					const glm::uvec3 grid(i % counts.x, (i / counts.x) % counts.y, i / (counts.x * counts.y));
					debugDrawSphere(min_corner + glm::vec3(grid) * step, dot_radius, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
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
				frame.frame_data.projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, m_camera->near_plane, m_camera->far_plane);
				frame.frame_data.view_projection = frame.frame_data.projection * frame.frame_data.view;
				frame.frame_data.camera_position = probe_position;
				frame.irradiance_capture_index = probe_index;
				frame.irradiance_capture_face = face;

				frame.capture_extent = std::min({k_irradiance_capture_size, extent.width, extent.height});

				frame.render_mode = 0;
				frame.frame_data.render_mode_pad.x = 0;

				frame.frame_data.irradiance_volume_count_pad.x = 0;

				++m_irradiance_bake_cursor;
			}
		}

		m_bake_active.store(m_probe_bake_cursor >= 0 || m_irradiance_bake_cursor >= 0, std::memory_order_relaxed);
	}

	frame.viewport_extent = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
	frame.camera_near = m_camera->near_plane;
	frame.camera_far = m_camera->far_plane;

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
			debugDrawMesh(cameraGizmoMesh(), camera->getWorldTransform());

			constexpr float k_camera_frustum_length = 3.0f;
			const auto extent = m_output_target->getExtent();
			const float aspect = extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;
			debugDrawFrustum(*camera, aspect, glm::vec4(1.0f, 1.0f, 0.2f, 1.0f), k_camera_frustum_length);
		}
	}

	glm::vec3 ambient_sum(0.0f);
	uint32_t directional_count = 0;
	frame.lights.reserve(light_nodes_snapshot.size());

	int32_t shadow_caster_index = -1;
	glm::vec3 shadow_caster_direction {0.0f};

	auto& spot_candidates = m_tick_spot_candidates;
	auto& point_candidates = m_tick_point_candidates;
	spot_candidates.clear();
	point_candidates.clear();

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

		const bool casts_shadows =
		    light->castsShadows() && (!positional || light->shadowDistance() <= 0.0f || camera_distance <= light->shadowDistance());

		constexpr float k_light_icon_size = 0.5f;
		debugDrawBillboard(light->world_position, k_light_icon_size, lightIcon(light->lightType()), glm::vec4(light->color(), 1.0f));

		switch (light->lightType()) {
			case toast::LightType::ambient: {
				ambient_sum += light->color() * light->intensity();
				break;
			}
			case toast::LightType::directional: {
				debugDrawArrow(light->world_position, light->world_position + light->forward() * 2.0f, glm::vec4(light->color(), 1.0f));

				if (directional_count >= k_max_directional_lights) {
					break;
				}
				frame.frame_data.directional_lights[directional_count] = DirectionalLightData {
				  .direction = glm::vec4(light->forward(), 0.0f),
				  .color_intensity = glm::vec4(light->color(), light->intensity()),
				};
				if (shadow_caster_index < 0 && casts_shadows) {
					shadow_caster_index = static_cast<int32_t>(directional_count);
					shadow_caster_direction = light->forward();
				}
				++directional_count;
				break;
			}
			case toast::LightType::point: {
				auto* point = static_cast<toast::PointLight*>(light);
				const glm::vec3 world_pos = point->world_position;
				const glm::vec3 view_pos = glm::vec3(camera_view * glm::vec4(world_pos, 1.0f));

				debugDrawSphere(world_pos, point->attenuation(), glm::vec4(point->color(), 1.0f));

				if (casts_shadows) {
					point_candidates.push_back(
					    PunctualShadowCandidate {
					      .light_index = frame.lights.size(),
					      .distance_squared = camera_distance * camera_distance,
					      .resolution_scale = point->shadowResolutionScale(),
					    }
					);
				}

				frame.lights.push_back(
				    GpuLight {
				      .world_pos_range = glm::vec4(world_pos, point->attenuation()),
				      .view_pos_type = glm::vec4(view_pos, 0.0f),
				      .color_intensity = glm::vec4(point->color(), point->intensity()),
				      .direction_pad = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f),
				      .cone_angles = glm::vec4(0.0f),
				    }
				);
				break;
			}
			case toast::LightType::spot: {
				auto* spot = static_cast<toast::Spotlight*>(light);
				const glm::vec3 world_pos = spot->world_position;
				const glm::vec3 view_pos = glm::vec3(camera_view * glm::vec4(world_pos, 1.0f));

				debugDrawCone(world_pos, spot->forward(), spot->attenuation(), spot->outerRadius(), glm::vec4(spot->color(), 1.0f));

				if (casts_shadows) {
					spot_candidates.push_back(
					    PunctualShadowCandidate {
					      .light_index = frame.lights.size(),
					      .distance_squared = camera_distance * camera_distance,
					      .resolution_scale = spot->shadowResolutionScale(),
					    }
					);
				}

				frame.lights.push_back(
				    GpuLight {
				      .world_pos_range = glm::vec4(world_pos, spot->attenuation()),
				      .view_pos_type = glm::vec4(view_pos, 1.0f),
				      .color_intensity = glm::vec4(spot->color(), spot->intensity()),
				      .direction_pad = glm::vec4(spot->forward(), -1.0f),
				      .cone_angles =
				          glm::vec4(std::cos(glm::radians(spot->outerRadius())), std::cos(glm::radians(spot->innerRadius())), 0.0f, 0.0f),
				    }
				);
				break;
			}
			default: break;
		}
	}

	frame.frame_data.ambient_color_intensity = glm::vec4(ambient_sum, 1.0f);
	frame.frame_data.directional_light_count_pad.x = directional_count;

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
				debugDrawBox(probe->world_position - probe->boxExtents(), probe->world_position + probe->boxExtents(), volume_color);
			} else {
				debugDrawSphere(probe->world_position, probe->influenceRadius(), volume_color);
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

	if (m_environment_pass != nullptr && m_environment_pass->isReady()) {
		frame.frame_data.environment_params.x = 1.0f;
		frame.frame_data.environment_params.y = static_cast<float>(m_environment_pass->getPrefilteredMipCount());
	}

	{
		auto state = std::tuple {
		  ambient_sum.r + ambient_sum.g + ambient_sum.b,
		  directional_count,
		  frame.lights.size(),
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
			TOAST_INFO(
			    "Render",
			    "Lighting inputs: ambient=({:.3f}, {:.3f}, {:.3f}), {} directional, {} punctual, environment={}, "
			    "{} mesh instances, render mode {}, exposure {:.3f}, {} probes (baked mask {:#x}),",
			    ambient_sum.r,
			    ambient_sum.g,
			    ambient_sum.b,
			    directional_count,
			    frame.lights.size(),
			    frame.frame_data.environment_params.x >= 0.5f ? "ready" : "absent",
			    frame.mesh_instances.size(),
			    frame.render_mode,
			    frame.post_process.tonemap.exposure,
			    frame.frame_data.reflection_probe_count_pad.x,
			    frame.frame_data.reflection_probe_count_pad.z
			);
		}
	}

	const bool trace_shadows =
	    m_core->isRayTracingSupported() && tracedShadowsEnabled() && std::ranges::any_of(frame.mesh_instances, isTraceable);
	frame.frame_data.traced_shadow_params.x = trace_shadows ? 1.0f : 0.0f;

	if (!trace_shadows) {
		fitShadowViews(frame, aspect, shadow_caster_index, shadow_caster_direction, spot_candidates, point_candidates);
	}

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

	{
		const bool is_capture = frame.probe_capture_index >= 0 || frame.irradiance_capture_index >= 0;

		const bool freeze = m_cull_freeze.load(std::memory_order_relaxed) && !is_capture;
		if (!freeze) {
			m_has_frozen_cull = false;
		} else if (!m_has_frozen_cull) {
			m_frozen_cull_view_projection = frame.frame_data.view_projection;
			m_has_frozen_cull = true;
		}

		const glm::mat4 cull_view_projection = freeze ? m_frozen_cull_view_projection : frame.frame_data.view_projection;
		const auto planes = extractFrustumPlanes(cull_view_projection);

		uint32_t visible = 0;
		for (auto& proxy : frame.mesh_instances) {
			proxy.visible = proxy.bounds_radius <= 0.0f || sphereInFrustum(planes, proxy.bounds_center, proxy.bounds_radius);
			visible += proxy.visible ? 1u : 0u;
		}
		m_visible_instance_count.store(visible, std::memory_order_relaxed);

		const glm::vec3 eye = frame.frame_data.camera_position;
		const float near_plane = m_camera->near_plane;
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
			frame.shadow_instance_data.push_back(InstanceData {.model = instance_model_of(proxy), .joint_offset = proxy.joint_offset});
		}

		for (auto& proxy : frame.mesh_instances) {
			if (!proxy.visible) {
				continue;
			}
			if (frame.instance_data.size() >= k_max_instances) {
				proxy.visible = false;
				continue;
			}
			proxy.instance_index = static_cast<uint32_t>(frame.instance_data.size());
			frame.instance_data.push_back(InstanceData {.model = instance_model_of(proxy), .joint_offset = proxy.joint_offset});
		}

		if (m_cull_debug_draw.load(std::memory_order_relaxed) && !is_capture) {
			debugDrawFrustumFromMatrix(cull_view_projection, glm::vec4(1.0f, 1.0f, 0.2f, 1.0f));

			constexpr int k_debug_sphere_segments = 8;
			for (const auto& proxy : frame.mesh_instances) {
				if (proxy.bounds_radius <= 0.0f) {
					continue;
				}
				const glm::vec4 color = proxy.visible ? glm::vec4(0.2f, 1.0f, 0.3f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.2f, 1.0f);
				debugDrawSphere(proxy.bounds_center, proxy.bounds_radius, color, k_debug_sphere_segments);
			}

			for (const auto& proxy : frame.voxel_instances) {
				const glm::vec4 color = proxy.visible ? glm::vec4(0.2f, 0.85f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.2f, 1.0f);
				debugDrawSphere(proxy.bounds_center, proxy.bounds_radius, color, k_debug_sphere_segments);
			}
		}
	}

	if (m_ui_frame_builder) {
		m_ui_frame_builder(frame);
	}

	count_build();
	submitFrame();
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

namespace {

[[nodiscard]]
auto defaultVoxelPalette() -> const voxel::Palette& {
	static const voxel::Palette palette = [] {
		voxel::Palette out;
		for (uint32_t i = 1; i < voxel::k_palette_size; ++i) {
			out.entries[i].albedo_r = 160;
			out.entries[i].albedo_g = 160;
			out.entries[i].albedo_b = 160;
			out.entries[i].roughness = 200;
		}
		return out;
	}();
	return palette;
}

}

void VulkanRenderer::buildVoxelProxies(RenderFrame& frame) {
	ZoneScoped;

	auto& nodes = m_tick_voxel_nodes;
	{
		std::scoped_lock lock(m_voxel_proxy_mutex);
		nodes.assign(m_voxel_proxy_nodes.begin(), m_voxel_proxy_nodes.end());
	}

	if (m_voxel_storage_pending) {
		if (m_voxel_storage_pending->isReady()) {
			m_voxel_storage = std::move(m_voxel_storage_pending);
			m_voxel_storage_pending.reset();
		} else if (m_voxel_storage_pending->hasFailed()) {
			if (!m_voxel_upload_failed_warned) {
				m_voxel_upload_failed_warned = true;
				TOAST_ERROR("Render", "Voxel scene upload failed; voxel volumes draw from the previous upload, if any");
			}
			m_voxel_storage_pending.reset();
		}
	}

	struct Gathered {
		toast::VoxelNode* node;
		voxel::Volume* volume;
		const voxel::Palette* palette;
	};

	std::vector<Gathered> gathered;
	std::vector<VoxelSceneKey> key;
	gathered.reserve(nodes.size());
	key.reserve(nodes.size());

	for (auto* node : nodes) {
		if (node == nullptr || !node->enabled()) {
			continue;
		}
		if (m_render_owner_filter != nullptr && node->owner() != m_render_owner_filter) {
			continue;
		}

		voxel::Volume* volume = node->volume();
		if (volume == nullptr) {
			continue;
		}
		const voxel::Palette* palette = node->resolvedPalette();
		if (palette == nullptr) {
			palette = &defaultVoxelPalette();
		}

		gathered.push_back({node, volume, palette});
		key.push_back({.node_uid = node->uid().data(), .revision = node->revision(), .palette = palette});
	}

	if (key != m_voxel_scene_key) {
		m_voxel_scene_key = key;
		m_voxel_upload_failed_warned = false;

		if (gathered.empty()) {
			m_voxel_storage.reset();
			m_voxel_storage_pending.reset();
		} else {
			std::vector<voxel::gpu::SceneVolume> scene_volumes;
			std::vector<uint64_t> node_uids;
			std::vector<glm::uvec3> brick_dims;
			scene_volumes.reserve(gathered.size());
			node_uids.reserve(gathered.size());
			brick_dims.reserve(gathered.size());
			for (const Gathered& entry : gathered) {
				scene_volumes.push_back({.volume = entry.volume, .palette = entry.palette});
				node_uids.push_back(entry.node->uid().data());
				brick_dims.push_back(entry.volume->brickDims());
			}

			auto storage = std::make_shared<VoxelGpuStorage>(std::move(node_uids), std::move(brick_dims));
			queueResourceUpload(
			    std::make_unique<VoxelSceneUpload>(
			        storage, voxel::gpu::packPool(voxel::runtimeBrickPool()), voxel::gpu::packScene(scene_volumes)
			    )
			);
			m_voxel_storage_pending = std::move(storage);
		}
	}

	frame.voxel_storage = m_voxel_storage;
	if (!m_voxel_storage) {
		m_voxel_previous_models.clear();
		return;
	}

	std::unordered_map<uint64_t, glm::mat4> drawn_models;
	drawn_models.reserve(gathered.size());
	frame.voxel_instances.reserve(gathered.size());

	for (const Gathered& entry : gathered) {
		const uint64_t node_uid = entry.node->uid().data();
		const std::optional<uint32_t> record = m_voxel_storage->recordIndexOf(node_uid);
		if (!record.has_value()) {
			continue;
		}

		const glm::uvec3 dims = m_voxel_storage->recordBrickDims(*record);
		const glm::mat4 model = entry.node->getWorldTransform();
		const glm::vec4 sphere = voxel::worldBoundingSphere(model, dims);
		const auto previous = m_voxel_previous_models.find(node_uid);

		frame.voxel_instances.push_back(
		    VoxelVolumeProxy {
		      .record_index = *record,
		      .brick_dims = dims,
		      .model = model,
		      .inverse_model = glm::inverse(model),
		      .previous_model = previous != m_voxel_previous_models.end() ? previous->second : model,
		      .bounds_center = glm::vec3(sphere),
		      .bounds_radius = sphere.w,
		      .mirrored = glm::determinant(model) < 0.0f,
		      .node_uid = node_uid,
		    }
		);
		drawn_models.insert_or_assign(node_uid, model);
	}

	m_voxel_previous_models.swap(drawn_models);
}

void VulkanRenderer::requestMaterialFrameSetRebuild() {
	std::lock_guard lock(m_pass_mutex);
	for (auto& [material, pass] : m_material_passes) {
		pass->markShadersDirty();
	}
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

namespace {

void drawOrientedBox(const glm::mat4& transform, const glm::vec3& extents, const glm::vec4& color) {
	std::array<glm::vec3, 8> corners {};
	for (size_t i = 0; i < corners.size(); ++i) {
		const glm::vec3 sign {(i & 1u) != 0 ? 1.0f : -1.0f, (i & 2u) != 0 ? 1.0f : -1.0f, (i & 4u) != 0 ? 1.0f : -1.0f};
		corners[i] = glm::vec3(transform * glm::vec4(sign * extents, 1.0f));
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		debugDrawLine(corners[a], corners[b], color);
	}
}

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
			drawOrientedBox(volume->getWorldTransform(), volume->extents(), color);
		}

		if (influence <= 0.0f) {
			continue;
		}
		blendPostProcess(result, volume->settings(), influence);
	}

	return result;
}

void VulkanRenderer::cancelIrradianceBake() {
	if (m_irradiance_bake_cursor < 0) {
		return;
	}

	TOAST_INFO("Render", "Irradiance volume set changed mid-bake; cancelling so bases can be reassigned");
	m_irradiance_bake_cursor = -1;
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
	}

	m_gpu_timer.reset();

#ifdef TRACY_ENABLE
	if (m_tracy_vk_ctx != nullptr) {
		TracyVkDestroy(m_tracy_vk_ctx);
		m_tracy_vk_ctx = nullptr;
	}
#endif
}

auto VulkanRenderer::applyResize(vk::Extent2D extent) -> void {
	if (extent.width == 0 || extent.height == 0) {
		return;
	}

	m_pending_resize_packed.store(packExtent(extent), std::memory_order_release);
	m_frame_cv.notify_one();
}

auto VulkanRenderer::applyResizeInternal(vk::Extent2D extent) -> void {
	ZoneScoped;
	if (!m_output_target->isPresentable()) {
		uint64_t expected = k_no_pending_resize;
		m_pending_resize_packed.compare_exchange_strong(expected, packExtent(extent), std::memory_order_acq_rel);
		return;
	}

	m_core->getDevice().waitIdle();
	publishCompletedFrames();
	m_pending_publish.clear();
	for (auto& frame : m_frames) {
		frame.has_submitted = false;
	}
	try {
		m_output_target->recreate(extent);
		const vk::Extent2D actual_extent = m_output_target->getExtent();
		const auto image_count = m_output_target->getImageCount();
		m_images_in_flight.assign(image_count, vk::Fence {});
		m_output_image_layouts.assign(image_count, vk::ImageLayout::eUndefined);
		createPerImageSync();
		createDepthResources();
		createSceneColorResources();

		{
			std::lock_guard lock(m_pass_mutex);
			for (auto& pass : m_post_process_passes) {
				pass->onResize(actual_extent);
			}
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

void VulkanRenderer::queueResourceUpload(std::unique_ptr<PendingResourceUpload> upload_job) {
	{
		std::lock_guard<std::mutex> lock(m_upload_mutex);
		m_upload_waiting.push_back(std::move(upload_job));
	}
	pumpUploadQueue();
}

void VulkanRenderer::pumpUploadQueue() {
	ZoneScoped;

	while (true) {
		std::unique_ptr<PendingResourceUpload> job;
		{
			std::lock_guard<std::mutex> lock(m_upload_mutex);
			if (m_upload_waiting.empty()) {
				return;
			}
			if (m_upload_host_bytes.load(std::memory_order_acquire) >= k_upload_host_budget) {
				return;
			}
			job = std::move(m_upload_waiting.front());
			m_upload_waiting.pop_front();
		}

		m_pending_upload_builds.fetch_add(1, std::memory_order_relaxed);
		toast::ThreadPool::push([this, j = std::move(job)]() mutable {
			j->build(*m_core);
			m_upload_host_bytes.fetch_add(j->host_bytes, std::memory_order_acq_rel);

			{
				std::lock_guard<std::mutex> lock(m_upload_mutex);
				m_upload_staging.push_back(std::move(j));
			}
			m_pending_upload_builds.fetch_sub(1, std::memory_order_acq_rel);
		});
	}
}

void VulkanRenderer::processPendingUploads() {
	ZoneScoped;
	const auto& device = m_core->getDevice();

	while (!m_pending_uploads.empty()) {
		auto& oldest_batch = m_pending_uploads.front();
		auto& slot = m_upload_slots[oldest_batch.slot];

		if (vkGetFenceStatus(*device, *slot.fence) != VkResult::VK_SUCCESS) {
			break;
		}

		vk::DeviceSize reclaimed = 0;
		for (auto& job : oldest_batch.jobs) {
			job->finished();
			reclaimed += job->host_bytes;
		}
		m_upload_host_bytes.fetch_sub(reclaimed, std::memory_order_acq_rel);

		slot.in_flight = false;
		m_pending_uploads.pop();
	}

	pumpUploadQueue();
}

void VulkanRenderer::flushResourceUploads() {
	ZoneScoped;
	if (m_upload_slots.empty()) {
		return;
	}

	UploadSlot& slot = m_upload_slots[m_next_upload_slot];
	if (slot.in_flight) {
		return;
	}

	std::vector<std::unique_ptr<PendingResourceUpload>> jobs_to_flush;
	{
		std::lock_guard<std::mutex> lock(m_upload_mutex);
		if (m_upload_staging.empty()) {
			return;
		}
		jobs_to_flush = std::move(m_upload_staging);
		m_upload_staging.clear();
	}

	const auto& device = m_core->getDevice();

	device.resetFences(*slot.fence);
	slot.command_buffer.reset();
	slot.command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	for (auto& job : jobs_to_flush) {
		job->record(*slot.command_buffer);
	}

	slot.command_buffer.end();

	BatchedUploadGroup batch;
	batch.slot = m_next_upload_slot;
	batch.jobs = std::move(jobs_to_flush);

	const vk::CommandBuffer raw_transfer_cmd = *slot.command_buffer;
	const vk::SubmitInfo submit_info(0, nullptr, nullptr, 1, &raw_transfer_cmd);
	m_core->getTransferQueue().submit(submit_info, *slot.fence);

	slot.in_flight = true;
	m_next_upload_slot = (m_next_upload_slot + 1) % k_upload_slots;

	m_pending_uploads.push(std::move(batch));
}

void VulkanRenderer::setActiveCamera(toast::Camera* camera) {
	m_camera = camera;
}

void VulkanRenderer::forgetCamera(const toast::Camera* camera) {
	if (m_camera == camera) {
		m_camera = nullptr;
	}
}

void debugDrawFrustum(const toast::Camera& camera, float aspect, glm::vec4 color, float far_override) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const float far_distance = far_override > 0.0f ? std::min(far_override, camera.far_plane) : camera.far_plane;

	const float tan_half_fov_y = std::tan(glm::radians(camera.fov) * 0.5f);
	const float near_height = 2.0f * tan_half_fov_y * camera.near_plane;
	const float near_width = near_height * aspect;
	const float far_height = 2.0f * tan_half_fov_y * far_distance;
	const float far_width = far_height * aspect;

	const std::array<glm::vec3, 8> view_space_corners {
	  glm::vec3 {-near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f, -near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 {-near_width * 0.5f,  near_height * 0.5f, -camera.near_plane},
	  glm::vec3 { -far_width * 0.5f,  -far_height * 0.5f,      -far_distance},
	  glm::vec3 {  far_width * 0.5f,  -far_height * 0.5f,      -far_distance},
	  glm::vec3 {  far_width * 0.5f,   far_height * 0.5f,      -far_distance},
	  glm::vec3 { -far_width * 0.5f,   far_height * 0.5f,      -far_distance},
	};

	const glm::mat4 inv_view = glm::inverse(camera.getView());
	std::array<glm::vec3, 8> world_corners {};
	for (int i = 0; i < 8; ++i) {
		world_corners[i] = glm::vec3(inv_view * glm::vec4(view_space_corners[i], 1.0f));
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		debugDrawLine(world_corners[a], world_corners[b], color);
	}
}

void debugDrawFrustumFromMatrix(const glm::mat4& view_projection, glm::vec4 color) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const glm::mat4 inverse = glm::inverse(view_projection);

	static constexpr std::array<glm::vec3, 8> ndc_corners {
	  glm::vec3 {-1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f, -1.0f, 0.0f},
	  glm::vec3 { 1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f,  1.0f, 0.0f},
	  glm::vec3 {-1.0f, -1.0f, 1.0f},
	  glm::vec3 { 1.0f, -1.0f, 1.0f},
	  glm::vec3 { 1.0f,  1.0f, 1.0f},
	  glm::vec3 {-1.0f,  1.0f, 1.0f},
	};

	std::array<glm::vec3, 8> world_corners {};
	for (int i = 0; i < 8; ++i) {
		const glm::vec4 unprojected = inverse * glm::vec4(ndc_corners[static_cast<size_t>(i)], 1.0f);
		world_corners[static_cast<size_t>(i)] = glm::vec3(unprojected) / unprojected.w;
	}

	static constexpr std::array<std::pair<int, int>, 12> edges {
	  {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
	};
	for (const auto& [a, b] : edges) {
		debugDrawLine(world_corners[static_cast<size_t>(a)], world_corners[static_cast<size_t>(b)], color);
	}
}

void debugDrawMesh(toast::UID mesh, const glm::mat4& transform, glm::vec4 tint) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	debugDrawMesh(assets::load<assets::Mesh>(mesh), transform, tint);
}

void debugDrawBillboard(glm::vec3 world_position, float size, toast::UID texture, glm::vec4 tint) {
	if (texture.data() == 0 || !VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	debugDrawBillboard(world_position, size, assets::load<assets::Texture>(texture), tint);
}

void debugDrawCone(glm::vec3 apex, glm::vec3 direction, float length, float half_angle_degrees, glm::vec4 color, int segments) {
	if (!VulkanRenderer::instance->debugDrawEnabled()) {
		return;
	}
	const float dir_len = glm::length(direction);
	const glm::vec3 axis = dir_len > 0.0001f ? direction / dir_len : glm::vec3(0.0f, 0.0f, -1.0f);

	const glm::vec3 up = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 u = glm::normalize(glm::cross(up, axis));
	const glm::vec3 w = glm::cross(axis, u);

	const float radius = length * std::tan(glm::radians(half_angle_degrees));
	const glm::vec3 base_center = apex + axis * length;

	std::vector<glm::vec3> ring(static_cast<size_t>(segments));
	for (int i = 0; i < segments; ++i) {
		const float t = (static_cast<float>(i) / static_cast<float>(segments)) * glm::two_pi<float>();
		ring[static_cast<size_t>(i)] = base_center + (u * std::cos(t) + w * std::sin(t)) * radius;
	}

	for (int i = 0; i < segments; ++i) {
		const int next = (i + 1) % segments;
		debugDrawLine(ring[static_cast<size_t>(i)], ring[static_cast<size_t>(next)], color);
	}

	constexpr int k_spokes = 4;
	for (int i = 0; i < k_spokes; ++i) {
		const int idx = (i * segments) / k_spokes;
		debugDrawLine(apex, ring[static_cast<size_t>(idx)], color);
	}
}

}
