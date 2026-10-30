/// @file vulkan_renderer_resources.cpp
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

	submitAndWait(device, core.getGraphicsQueue(), core.graphicsSubmitMutex(), *cmd);

	texture.markReady();
}

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

auto VulkanRenderer::createUploadRing() -> void {
	m_upload_slots.clear();
	m_upload_slots.resize(k_upload_slots);
	m_next_upload_slot = 0;

	const vk::CommandBufferAllocateInfo allocate_ci(*m_transfer_command_pool, vk::CommandBufferLevel::ePrimary, k_upload_slots);
	auto buffers = m_core->getDevice().allocateCommandBuffers(allocate_ci);

	for (uint32_t i = 0; i < k_upload_slots; ++i) {
		m_upload_slots[i].command_buffer = std::move(buffers[i]);
		setDebugName(*m_core, *m_upload_slots[i].command_buffer, std::format("VulkanRenderer Upload[{}] CommandBuffer", i));
	}

	vk::SemaphoreTypeCreateInfo timeline_ci(vk::SemaphoreType::eTimeline, 0);
	m_upload_timeline = vk::raii::Semaphore(m_core->getDevice(), vk::SemaphoreCreateInfo({}, &timeline_ci));
	setDebugName(*m_core, *m_upload_timeline, "VulkanRenderer UploadTimeline");
	m_upload_timeline_value = 0;
	m_upload_acquired_value = 0;
	m_upload_waited_value = 0;

	TOAST_TRACE("VulkanRenderer", "Upload command buffer ring created: {} slots", k_upload_slots);
}

auto VulkanRenderer::createFrameContexts() -> void {
	ZoneScoped;
	m_frames.clear();
	m_frames.resize(k_frames_in_flight);

	const vk::SemaphoreCreateInfo semaphore_ci {};
	const vk::FenceCreateInfo fence_ci(vk::FenceCreateFlagBits::eSignaled);
	const vk::CommandBufferAllocateInfo command_buffer_ci(*m_command_pool, vk::CommandBufferLevel::ePrimary, k_frames_in_flight);
	auto allocated_command_buffers = m_core->getDevice().allocateCommandBuffers(command_buffer_ci);

	for (uint32_t frame_index = 0; frame_index < k_frames_in_flight; ++frame_index) {
		m_frames[frame_index].command_buffer = std::move(allocated_command_buffers[frame_index]);
		m_frames[frame_index].image_available = vk::raii::Semaphore(m_core->getDevice(), semaphore_ci);
		m_frames[frame_index].in_flight = vk::raii::Fence(m_core->getDevice(), fence_ci);

		setDebugName(
		    *m_core, *m_frames[frame_index].command_buffer, std::format("VulkanRenderer Frame[{}] CommandBuffer", frame_index)
		);
		setDebugName(
		    *m_core, *m_frames[frame_index].image_available, std::format("VulkanRenderer Frame[{}] ImageAvailable", frame_index)
		);
		setDebugName(*m_core, *m_frames[frame_index].in_flight, std::format("VulkanRenderer Frame[{}] InFlightFence", frame_index));
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

	const auto extent = renderExtent();
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
	image_ci.usage =
	    vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc;
	image_ci.sharingMode = vk::SharingMode::eExclusive;
	image_ci.initialLayout = vk::ImageLayout::eUndefined;

	vma::AllocationCreateInfo allocation_ci {};
	allocation_ci.usage = vma::MemoryUsage::eAutoPreferDevice;

	m_glass_backdrop_depth.view.reset();
	m_glass_backdrop_depth.image.reset();
	vk::ImageCreateInfo backdrop_ci = image_ci;
	backdrop_ci.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
	m_glass_backdrop_depth.image.emplace(m_core->getAllocator().createImage(backdrop_ci, allocation_ci));
	setDebugName(*m_core, **m_glass_backdrop_depth.image, "VulkanRenderer GlassBackdropDepthImage");
	vk::ImageViewCreateInfo backdrop_view_ci {};
	backdrop_view_ci.image = **m_glass_backdrop_depth.image;
	backdrop_view_ci.viewType = vk::ImageViewType::e2D;
	backdrop_view_ci.format = m_depth_format;
	backdrop_view_ci.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
	m_glass_backdrop_depth.view.emplace(m_core->getDevice(), backdrop_view_ci);
	setDebugName(*m_core, **m_glass_backdrop_depth.view, "VulkanRenderer GlassBackdropDepthView");
	m_glass_backdrop_depth.state.reset(**m_glass_backdrop_depth.image, depthAttachmentRange(m_depth_format));

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

	m_depth_resources.state.reset(**m_depth_resources.image, depthAttachmentRange(m_depth_format));

	m_output_depth.view.reset();
	m_output_depth.image.reset();
	const vk::Extent2D output_extent = m_output_target->getExtent();
	if (output_extent != extent) {
		vk::ImageCreateInfo output_ci = image_ci;
		output_ci.extent = vk::Extent3D {output_extent.width, output_extent.height, 1};
		output_ci.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
		m_output_depth.image.emplace(m_core->getAllocator().createImage(output_ci, allocation_ci));
		setDebugName(*m_core, **m_output_depth.image, "VulkanRenderer OutputDepthImage");
		vk::ImageViewCreateInfo output_view_ci = view_ci;
		output_view_ci.image = **m_output_depth.image;
		m_output_depth.view.emplace(m_core->getDevice(), output_view_ci);
		setDebugName(*m_core, **m_output_depth.view, "VulkanRenderer OutputDepthView");
		m_output_depth.state.reset(**m_output_depth.image, depthAttachmentRange(m_depth_format));
	}
	TOAST_TRACE(
	    "Render", "Depth resources created at {}x{} with format {}", extent.width, extent.height, vk::to_string(m_depth_format)
	);
}

void VulkanRenderer::createSceneColorResources() {
	ZoneScoped;
	const auto extent = renderExtent();
	if (extent.width == 0 || extent.height == 0) {
		TOAST_CRITICAL("Render", "Toast Engine Error: VulkanRenderer requires a non-zero output extent for the scene target!");
	}

	const auto create = [&](
	                        SceneColorResources& target, vk::Format format, std::string_view label, vk::ImageUsageFlags usage = {}
	                    ) {
		target.view.reset();
		target.image.reset();

		const auto image_ci = colorTargetImageInfo(extent, format, usage);

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

		target.state.reset(**target.image, colorSubresourceRange());
		TOAST_TRACE("Render", "{} target created at {}x{} with format {}", label, extent.width, extent.height, vk::to_string(format));
	};

	create(m_scene_color, m_scene_color_format, "SceneColor", vk::ImageUsageFlagBits::eTransferSrc);
	create(m_glass_backdrop, m_scene_color_format, "GlassBackdrop", vk::ImageUsageFlagBits::eTransferDst);
	create(m_scene_normal, m_scene_normal_format, "SceneNormal", vk::ImageUsageFlagBits::eTransferSrc);
	create(m_glass_backdrop_normal, m_scene_normal_format, "GlassBackdropNormal", vk::ImageUsageFlagBits::eTransferDst);
	create(m_scene_indirect, m_scene_indirect_format, "SceneIndirect");
	create(m_scene_motion, m_scene_motion_format, "SceneMotion");
	create(m_voxel_albedo, k_voxel_albedo_format, "VoxelAlbedo");
	create(m_voxel_material, k_voxel_material_format, "VoxelMaterial");
	create(m_voxel_face_normal, k_voxel_face_normal_format, "VoxelFaceNormal");
	create(m_voxel_noisy_diffuse, k_voxel_noisy_format, "VoxelNoisyDiffuse");
	create(m_voxel_noisy_specular, k_voxel_noisy_format, "VoxelNoisySpecular");
	create(m_voxel_reservoirs[0], k_voxel_reservoir_format, "VoxelReservoirs0");
	create(m_voxel_reservoirs[1], k_voxel_reservoir_format, "VoxelReservoirs1");
	create(m_voxel_candidate_reservoirs, k_voxel_reservoir_format, "VoxelCandidateReservoirs");
	m_voxel_reservoir_written = false;
}

void VulkanRenderer::createDescriptorPool() {
	std::vector<vk::DescriptorPoolSize> pool_sizes {
	  vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 8192),

	  vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 32768),

	  vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 8192),

	  vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 256)
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

auto VulkanRenderer::selectDepthFormat(const VulkanCore& core) -> vk::Format {
	constexpr std::array candidates {
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

	VulkanPipeline::Config depth_config = config;
	depth_config.debug_name = "DepthUpscale";
	depth_config.fragment_entry = "depthMain";
	depth_config.depth_only = true;
	depth_config.depth_fragment = true;
	depth_config.depth_test = true;
	depth_config.depth_write = true;
	depth_config.depth_compare = vk::CompareOp::eAlways;
	m_depth_upscale_pipeline.rebuild(*m_core, depth_config);

	const auto& device = m_core->getDevice();

	const auto sampler_ci = linearClampSamplerInfo();
	m_present_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(*m_core, *m_present_sampler, "VulkanRenderer PresentSampler");
	m_depth_upscale_sampler = vk::raii::Sampler(device, nearestClampSamplerInfo());
	setDebugName(*m_core, *m_depth_upscale_sampler, "VulkanRenderer DepthUpscaleSampler");

	const auto& layouts = m_present_layout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		return;
	}

	const vk::DescriptorSetLayout set_layout = *layouts[0];
	m_present_sets.clear();
	m_depth_upscale_sets.clear();
	m_present_bound_views.assign(k_frames_in_flight, vk::ImageView {});
	for (uint32_t i = 0; i < k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(*m_descriptor_pool, 1, &set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_present_sets.push_back(std::move(allocated[0]));
		setDebugName(*m_core, *m_present_sets[i], std::format("VulkanRenderer PresentSet[{}]", i));
		auto depth_set = device.allocateDescriptorSets(alloc_info);
		m_depth_upscale_sets.push_back(std::move(depth_set[0]));
		setDebugName(*m_core, *m_depth_upscale_sets[i], std::format("VulkanRenderer DepthUpscaleSet[{}]", i));
	}
	m_depth_upscale_bound.assign(k_frames_in_flight, vk::ImageView {});
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

	m_fog_volume_res.resize(k_frames_in_flight);
	for (uint32_t i = 0; i < m_fog_volume_res.size(); ++i) {
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = sizeof(FogVolumeGpu) * k_max_fog_volumes;
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_fog_volume_res[i].gpu_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, **m_fog_volume_res[i].gpu_buffer, std::format("VulkanRenderer FogVolumes[{}]", i));
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

	{
		const std::vector<uint32_t> noise = blue_noise::generatePacked();
		vk::BufferCreateInfo buffer_ci {};
		buffer_ci.size = noise.size() * sizeof(uint32_t);
		buffer_ci.usage = vk::BufferUsageFlagBits::eStorageBuffer;

		vma::AllocationCreateInfo alloc_ci {};
		alloc_ci.usage = vma::MemoryUsage::eAutoPreferHost;
		alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		m_blue_noise_buffer.emplace(m_core->getAllocator().createBuffer(buffer_ci, alloc_ci));
		setDebugName(*m_core, **m_blue_noise_buffer, "VulkanRenderer BlueNoise");
		const auto& allocation = m_blue_noise_buffer->getAllocation();
		std::memcpy(allocation.getInfo().pMappedData, noise.data(), buffer_ci.size);
		allocation.flush(0, buffer_ci.size);
	}
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

	// Shadow maps keep standard depth so 1 is the far plane and every less-or-equal compare reads as lit
	constexpr vk::ClearDepthStencilValue clear_value(1.0f, 0);
	cmd.clearDepthStencilImage(**m_default_shadow_image, vk::ImageLayout::eTransferDstOptimal, clear_value, range);

	recordTransferDstToShaderRead(cmd, **m_default_shadow_image, range);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), m_core->graphicsSubmitMutex(), *cmd);

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

	constexpr vk::ClearColorValue clear_value(std::array {0.0f, 0.0f, 0.0f, 1.0f});
	cmd.clearColorImage(**m_default_cube_image, vk::ImageLayout::eTransferDstOptimal, clear_value, range);

	recordTransferDstToShaderRead(cmd, **m_default_cube_image, range);

	cmd.end();

	submitAndWait(device, m_core->getGraphicsQueue(), m_core->graphicsSubmitMutex(), *cmd);

	vk::ImageViewCreateInfo view_ci {};
	view_ci.image = **m_default_cube_image;
	view_ci.viewType = vk::ImageViewType::eCube;
	view_ci.format = vk::Format::eR16G16B16A16Sfloat;
	view_ci.subresourceRange = range;
	m_default_cube_view = vk::raii::ImageView(device, view_ci);
	setDebugName(*m_core, *m_default_cube_view, "VulkanRenderer DefaultCubemapView");
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

auto VulkanRenderer::uploadStats() -> UploadStats {
	UploadStats stats {};
	stats.slot_count = static_cast<uint32_t>(m_upload_slots.size());
	stats.slots_in_flight =
	    static_cast<uint32_t>(std::ranges::count_if(m_upload_slots, [](const UploadSlot& slot) { return slot.in_flight; }));
	stats.batches_in_flight = m_pending_uploads.size();
	stats.host_bytes = m_upload_host_bytes.load(std::memory_order_relaxed);
	stats.host_budget = k_upload_host_budget;
	{
		std::scoped_lock lock(m_upload_mutex);
		stats.waiting_jobs = m_upload_waiting.size();
	}
	return stats;
}

void VulkanRenderer::processPendingUploads() {
	ZoneScoped;
	const uint64_t completed = m_pending_uploads.empty() ? 0 : m_upload_timeline.getCounterValue();

	while (!m_pending_uploads.empty()) {
		auto& oldest_batch = m_pending_uploads.front();
		auto& slot = m_upload_slots[oldest_batch.slot];

		if (slot.timeline_value > completed) {
			break;
		}

		vk::DeviceSize reclaimed = 0;
		for (auto& job : oldest_batch.jobs) {
			// The timeline check above orders this after the upload release
			job->recordAcquire(m_pending_acquires);
			job->finished();
			reclaimed += job->host_bytes;
		}
		m_upload_host_bytes.fetch_sub(reclaimed, std::memory_order_acq_rel);

		slot.in_flight = false;
		m_upload_acquired_value = std::max(m_upload_acquired_value, slot.timeline_value);
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

	// Jobs that read another upload go back in front until that one is recorded
	const auto defer = [this](std::vector<std::unique_ptr<PendingResourceUpload>>& jobs) {
		if (jobs.empty()) {
			return;
		}
		std::lock_guard<std::mutex> lock(m_upload_mutex);
		m_upload_staging.insert(m_upload_staging.begin(), std::make_move_iterator(jobs.begin()), std::make_move_iterator(jobs.end()));
		jobs.clear();
	};

	if (std::ranges::none_of(jobs_to_flush, [](const auto& job) { return job->canRecord(); })) {
		defer(jobs_to_flush);
		return;
	}

	slot.command_buffer.reset();
	slot.command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	// Recording one job can free the next so repeat until nothing more can go
	std::vector<std::unique_ptr<PendingResourceUpload>> recorded;
	for (bool progressed = true; progressed && !jobs_to_flush.empty();) {
		progressed = false;
		for (auto job = jobs_to_flush.begin(); job != jobs_to_flush.end();) {
			if (!(*job)->canRecord()) {
				++job;
				continue;
			}
			(*job)->setQueueHandoff({m_core->getTransferQueueFamilyIndex(), m_core->getGraphicsQueueFamilyIndex()});
			(*job)->record(*slot.command_buffer);
			recorded.push_back(std::move(*job));
			job = jobs_to_flush.erase(job);
			progressed = true;
		}
	}
	defer(jobs_to_flush);

	slot.command_buffer.end();

	BatchedUploadGroup batch;
	batch.slot = m_next_upload_slot;
	batch.jobs = std::move(recorded);

	const vk::CommandBuffer raw_transfer_cmd = *slot.command_buffer;
	slot.timeline_value = ++m_upload_timeline_value;
	const sync::SemaphoreSignal done {*m_upload_timeline, slot.timeline_value};
	{
		// Only contended when AsyncCompute shares the transfer queue
		std::scoped_lock transfer_lock(m_core->transferSubmitMutex());
		sync::submit(
		    m_core->getTransferQueue(),
		    {
		      .commands = {&raw_transfer_cmd, 1},
            .signals = {            &done, 1}
		}
		);
	}

	slot.in_flight = true;
	m_next_upload_slot = (m_next_upload_slot + 1) % k_upload_slots;

	m_pending_uploads.push(std::move(batch));
}

}
