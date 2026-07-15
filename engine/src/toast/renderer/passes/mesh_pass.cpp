/// @file mesh_pass.cpp
/// @author dario
/// @date 08/06/2026.

#include "mesh_pass.hpp"

#include "../shader_compiler.hpp"
#include "../vulkan_core.hpp"
#include "../vulkan_debug.hpp"
#include "../vulkan_renderer.hpp"
#include "../vulkan_texture.hpp"
#include "toast/assets/assets.hpp"
#include "toast/assets/material.hpp"
#include "toast/assets/texture.hpp"
#include "toast/log.hpp"

#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <tuple>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

namespace toast::renderer {
MeshPass::MeshPass(
    const toast::renderer::VulkanCore& core, vk::Format color_format, vk::Format depth_format, vk::Extent2D extent
) {
	auto shader_spirv = toast::renderer::ShaderCompiler::compileShaderModuleFromSource("./mesh.slang");

	// Use hardcoded layout keyed by "mesh"
	shader_layout.rebuild(core, "mesh");

	toast::renderer::VulkanPipeline::Config config;
	config.pipeline_type = toast::renderer::VulkanPipeline::PipelineType::graphics;
	config.debug_name = "MeshPass";
	config.color_format = color_format;
	config.depth_format = depth_format;
	config.extent = extent;
	config.shader_spirv = std::move(shader_spirv.spirv);
	// store raw pipeline layout handle
	config.pipeline_layout = *shader_layout.getPipelineLayout();

	config.vertex_binding = toast::renderer::vertexBindingDescription();
	const auto vertex_attributes = toast::renderer::vertexAttributeDescriptions();
	config.vertex_attributes.assign(vertex_attributes.begin(), vertex_attributes.end());

	m_pipeline.rebuild(core, config);

	createResources(core);

	m_asset_listener.subscribe<event::ClearUnusedAssets>([this] {
		m_pending_material_cache_clear.store(true, std::memory_order_release);
	});
}

void MeshPass::update(uint32_t frame_index, float dt) {
	updateUBO(frame_index);
}

void MeshPass::record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) {
	(void)image_index;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	// Ensure descriptor sets have been allocated
	if (m_frame_descriptor_sets.size() != VulkanRenderer::k_frames_in_flight) {
		TOAST_ERROR(
		    "MeshPass",
		    "Descriptor sets not allocated for MeshPass (size={} expected={})",
		    m_frame_descriptor_sets.size(),
		    VulkanRenderer::k_frames_in_flight
		);
		return;
	}

	if (frame_index >= m_frame_descriptor_sets.size()) {
		TOAST_ERROR("MeshPass", "Frame index {} out of bounds for descriptor sets", frame_index);
		return;
	}

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	const auto& core = VulkanRenderer::instance->getCore();

	// Materials may have been destroyed by AssetManager::clearUnusedAssets() since the last frame; their
	// addresses (m_material_sets' key) could already be reused by new, unrelated Material instances, which
	// would silently serve a stale descriptor set for the wrong material. waitIdle() is safe to call here:
	// this is the render thread, the only thread that ever submits to a queue, so a concurrent vkQueueSubmit
	// from elsewhere can't happen. This only runs on the rare frame where assets were actually unloaded.
	if (m_pending_material_cache_clear.exchange(false, std::memory_order_acq_rel)) {
		core.getDevice().waitIdle();
		m_material_sets.clear();
	}

	// Set 0 (camera UBO) is the same for every draw this frame; bind it once. Its contents are refreshed
	// every frame by memcpy-ing into the underlying buffer (see VulkanRenderer::updateFrameResources), not
	// by rewriting the descriptor itself, so there is nothing to update here.
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *shader_layout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*m_frame_descriptor_sets[frame_index]},
	    {}
	);

	// Track the last-bound set 1 so we only issue a bindDescriptorSets when the material actually changes
	vk::DescriptorSet bound_material_set {};

	for (const auto& proxy : frame->mesh_instances) {
		if (proxy.mesh == nullptr || !proxy.mesh->isReady()) {
			continue;
		}

		const vk::DescriptorSet material_set = (proxy.material != nullptr && proxy.material->albedoMap().hasValue())
		                                           ? getMaterialDescriptorSet(core, *proxy.material)
		                                           : *m_default_material_set;

		if (material_set != bound_material_set) {
			cmd.bindDescriptorSets(
			    vk::PipelineBindPoint::eGraphics,
			    *shader_layout.getPipelineLayout(),
			    1,
			    std::array<vk::DescriptorSet, 1> {material_set},
			    {}
			);
			bound_material_set = material_set;
		}

		DrawPushConstants data {};
		data.model = proxy.model;
		data.color = proxy.material != nullptr ? proxy.material->color() : glm::vec4(1.0f);
		cmd.pushConstants(
		    *shader_layout.getPipelineLayout(),
		    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		    0,
		    sizeof(DrawPushConstants),
		    &data
		);

		proxy.mesh->bind(cmd);
		proxy.mesh->draw(cmd);
	}
}

void MeshPass::createResources(const toast::renderer::VulkanCore& core) {
	const auto& layouts = shader_layout.getDescriptorSetLayouts();
	if (layouts.size() < 2) {
		TOAST_CRITICAL("MeshPass", "ShaderLayout must provide both the frame (set 0) and material (set 1) descriptor set layouts");
		return;
	}

	const auto& device = core.getDevice();
	const vk::DescriptorPool pool = VulkanRenderer::instance->getDescriptorPoolHandle();

	// NOTE: allocateDescriptorSets() returns owning vk::raii::DescriptorSet objects, the pool was created with
	// eFreeDescriptorSet, so if we only kept the raw handle the descriptor set would be
	// freed back to the pool the instant the temporary vector went out of scope, leaving a dangling handle that
	// crashes later
	const vk::DescriptorSetLayout frame_set_layout = *layouts[0];
	m_frame_descriptor_sets.clear();
	m_frame_descriptor_sets.reserve(VulkanRenderer::k_frames_in_flight);

	for (uint32_t i = 0; i < VulkanRenderer::k_frames_in_flight; ++i) {
		const vk::DescriptorSetAllocateInfo alloc_info(pool, 1, &frame_set_layout);
		auto allocated = device.allocateDescriptorSets(alloc_info);
		m_frame_descriptor_sets.push_back(std::move(allocated[0]));
		setDebugName(core, *m_frame_descriptor_sets[i], std::format("MeshPass FrameSet[{}]", i));

		const auto* frame_res = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frame_res->gpu_buffer.has_value()) {
			TOAST_CRITICAL("MeshPass", "Frame UBO buffer missing for frame {}", i);
			continue;
		}

		const vk::DescriptorBufferInfo buffer_info(**frame_res->gpu_buffer, 0, sizeof(VulkanRenderer::FrameUBO));
		const vk::WriteDescriptorSet write(
		    *m_frame_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_info
		);
		device.updateDescriptorSets(write, {});
	}

	createDefaultMaterialResources(core);
}

void MeshPass::createDefaultMaterialResources(const toast::renderer::VulkanCore& core) {
	const auto& device = core.getDevice();

	// 1x1 opaque white pixel so meshes without a material
	toast::renderer::VulkanTexture::Params params {};
	params.format = vk::Format::eR8G8B8A8Unorm;
	params.extent = vk::Extent3D {1, 1, 1};
	params.mip_levels = 1;
	params.layer_count = 1;
	m_default_texture.create(core, params, "MeshPass DefaultWhiteTexture");

	const std::array<uint8_t, 4> white_pixel {255, 255, 255, 255};

	vk::BufferCreateInfo staging_ci {};
	staging_ci.size = white_pixel.size();
	staging_ci.usage = vk::BufferUsageFlagBits::eTransferSrc;

	vma::AllocationCreateInfo alloc_ci {};
	alloc_ci.usage = vma::MemoryUsage::eAuto;
	alloc_ci.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

	auto staging_buffer = core.getAllocator().createBuffer(staging_ci, alloc_ci);
	setDebugName(core, *staging_buffer, "MeshPass DefaultWhiteTexture StagingBuffer");
	std::memcpy(staging_buffer.getAllocation().getInfo().pMappedData, white_pixel.data(), white_pixel.size());

	// This only runs once, at startup, so a blocking one-time submit is fine
	vk::raii::CommandPool one_shot_pool(device, vk::CommandPoolCreateInfo({}, core.getGraphicsQueueFamilyIndex()));
	const vk::CommandBufferAllocateInfo cmd_alloc_info(*one_shot_pool, vk::CommandBufferLevel::ePrimary, 1);
	auto cmd_buffers = device.allocateCommandBuffers(cmd_alloc_info);
	vk::raii::CommandBuffer cmd = std::move(cmd_buffers[0]);

	cmd.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

	vk::ImageMemoryBarrier to_dst {};
	to_dst.oldLayout = vk::ImageLayout::eUndefined;
	to_dst.newLayout = vk::ImageLayout::eTransferDstOptimal;
	to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	to_dst.image = m_default_texture.getImage();
	to_dst.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
	to_dst.srcAccessMask = {};
	to_dst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, to_dst);

	vk::BufferImageCopy region {};
	region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
	region.imageExtent = vk::Extent3D {1, 1, 1};
	cmd.copyBufferToImage(*staging_buffer, m_default_texture.getImage(), vk::ImageLayout::eTransferDstOptimal, region);

	vk::ImageMemoryBarrier to_read = to_dst;
	to_read.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	to_read.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	to_read.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	to_read.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	cmd.pipelineBarrier(
	    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, to_read
	);

	cmd.end();

	const vk::raii::Fence fence(device, vk::FenceCreateInfo {});
	const vk::CommandBuffer raw_cmd = *cmd;
	vk::SubmitInfo submit {};
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &raw_cmd;
	core.getGraphicsQueue().submit(submit, *fence);
	std::ignore = device.waitForFences(*fence, true, std::numeric_limits<uint64_t>::max());

	m_default_texture.markReady();

	vk::SamplerCreateInfo sampler_ci {};
	sampler_ci.magFilter = vk::Filter::eNearest;
	sampler_ci.minFilter = vk::Filter::eNearest;
	sampler_ci.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeV = vk::SamplerAddressMode::eRepeat;
	sampler_ci.addressModeW = vk::SamplerAddressMode::eRepeat;
	sampler_ci.mipmapMode = vk::SamplerMipmapMode::eNearest;
	m_default_sampler = vk::raii::Sampler(device, sampler_ci);
	setDebugName(core, *m_default_sampler, "MeshPass DefaultSampler");

	const vk::DescriptorSetLayout material_set_layout = *shader_layout.getDescriptorSetLayouts()[1];
	const vk::DescriptorSetAllocateInfo alloc_info(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &material_set_layout);
	auto allocated = device.allocateDescriptorSets(alloc_info);
	m_default_material_set = std::move(allocated[0]);
	setDebugName(core, *m_default_material_set, "MeshPass DefaultMaterialSet");

	vk::DescriptorImageInfo image_info {};
	image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	image_info.imageView = m_default_texture.getView();
	image_info.sampler = *m_default_sampler;

	const vk::WriteDescriptorSet write(*m_default_material_set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
	device.updateDescriptorSets(write, {});
}

auto MeshPass::getMaterialDescriptorSet(const toast::renderer::VulkanCore& core, assets::Material& material)
    -> vk::DescriptorSet {
	auto& texture = material.albedoMap()->gpuTexture();
	if (!texture.isReady()) {
		return *m_default_material_set;
	}

	const vk::ImageView view = texture.getView();
	const VkSampler sampler = material.albedoSampler();
	if (!view || sampler == VK_NULL_HANDLE) {
		TOAST_WARN("MeshPass", "Material has a ready texture but an invalid view/sampler; using fallback material");
		return *m_default_material_set;
	}

	auto [it, inserted] = m_material_sets.try_emplace(&material);
	MaterialGpuBinding& binding = it->second;

	if (inserted) {
		const vk::DescriptorSetLayout material_set_layout = *shader_layout.getDescriptorSetLayouts()[1];
		const vk::DescriptorSetAllocateInfo alloc_info(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &material_set_layout);
		auto allocated = core.getDevice().allocateDescriptorSets(alloc_info);
		binding.set = std::move(allocated[0]);
		setDebugName(core, *binding.set, std::format("MeshPass MaterialSet ({})", material.albedoMap().path()));
	}

	// Only re-issue the descriptor write when the underlying image view actually changed, instead of on every draw/frame like the
	// previous implementation
	if (binding.bound_view != view) {
		vk::DescriptorImageInfo image_info {};
		image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		image_info.imageView = view;
		image_info.sampler = sampler;

		const vk::WriteDescriptorSet write(*binding.set, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &image_info);
		core.getDevice().updateDescriptorSets(write, {});
		binding.bound_view = view;
	}

	return *binding.set;
}

void MeshPass::updateUBO(uint32_t frame_index) { }
}    // namespace toast::renderer
