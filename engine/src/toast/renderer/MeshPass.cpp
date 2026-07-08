/// @file MeshPass.cpp
/// @author dario
/// @date 08/06/2026.

#include "MeshPass.hpp"

#include "core/shader_compiler.hpp"
#include "core/vulkan_core.hpp"
#include "core/vulkan_renderer.hpp"
#include "core/vulkan_texture.hpp"
#include "toast/assets/material.hpp"
#include "toast/assets/texture.hpp"
#include "toast/log.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

namespace toast::renderer {
MeshPass::MeshPass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent) {
	auto shaderSpirv = toast::renderer::ShaderCompiler::compileShaderModuleFromSource("./mesh.slang");

	// Use hardcoded layout keyed by "mesh"
	shaderLayout.rebuild(core, "mesh");

	toast::renderer::VulkanPipeline::Config config;
	config.pipeline_type = toast::renderer::VulkanPipeline::PipelineType::graphics;
	config.color_format = colorFormat;
	config.depth_format = depthFormat;
	config.extent = extent;
	config.shader_spirv = std::move(shaderSpirv.spirv);
	// store raw pipeline layout handle
	config.pipeline_layout = *shaderLayout.getPipelineLayout();

	m_pipeline.rebuild(core, config);

	createResources(core);
}

void MeshPass::update(uint32_t frame_index, float dt) {
	updateUBO(frame_index);
}

void MeshPass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
	(void)imageIndex;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	// Ensure descriptor sets have been allocated
	if (m_descriptor_sets.size() != VulkanRenderer::kFramesInFlight) {
		TOAST_ERROR(
		    "MeshPass",
		    "Descriptor sets not allocated for MeshPass (size={} expected={})",
		    m_descriptor_sets.size(),
		    VulkanRenderer::kFramesInFlight
		);
		return;
	}

	if (frameIndex >= m_descriptor_sets.size()) {
		TOAST_ERROR("MeshPass", "Frame index {} out of bounds for descriptor sets", frameIndex);
		return;
	}

	const FrameResources* frameUBO = VulkanRenderer::instance->getFrameUBORes(frameIndex);

	const auto* frame = VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	for (const auto& proxy : frame->mesh_instances) {
		if (proxy.mesh == nullptr || !proxy.mesh->isReady()) {
			continue;
		}

		// If the mesh has a material with an albedo texture, write it into this pass's descriptor set at binding 1
		if (proxy.material != nullptr && proxy.material->albedoMap().hasValue()) {
			auto& material = *proxy.material;

			// Get image view and sampler
			auto& texHandle = material.albedoMap();
			auto& texture = texHandle->gpuTexture();
			vk::ImageView imageView = texture.getView();
			VkSampler samplerHandle = static_cast<VkSampler>(*material.albedoSampler());

			// Validate handles
			if (imageView && samplerHandle != VK_NULL_HANDLE) {
				vk::DescriptorImageInfo imageInfo {};
				imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
				imageInfo.imageView = imageView;
				imageInfo.sampler = samplerHandle;

				vk::WriteDescriptorSet write(
				    m_descriptor_sets[frameIndex], 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &imageInfo
				);

				// FIX: Changed nullptr to {} to prevent vulkan.hpp from treating it as a 1-element null array pointer
				auto& device = VulkanRenderer::instance->getCore().getDevice();
				device.updateDescriptorSets(write, {});
			} else {
				TOAST_WARN("MeshPass", "Skipping descriptor update: invalid imageView or sampler");
			}
		}

		DrawPushConstants data {};
		data.model = proxy.model;

		// Bind descriptor set for this draw (NOTE: Mutating and binding the same descriptor set
		// in a loop will cause all objects to draw with the final mesh's texture. Consider
		// switching to per-material descriptor sets or bindless rendering later).
		cmd.bindDescriptorSets(
		    vk::PipelineBindPoint::eGraphics,
		    *shaderLayout.getPipelineLayout(),
		    0,
		    std::array<vk::DescriptorSet, 1> {m_descriptor_sets[frameIndex]},
		    {}
		);

		cmd.pushConstants(*shaderLayout.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(DrawPushConstants), &data);

		proxy.mesh->bind(cmd);
		proxy.mesh->draw(cmd);
	}
}

void MeshPass::createResources(const toast::renderer::VulkanCore& core) {
	// Allocate per-frame descriptor sets using the ShaderLayout's set 0 layout
	const auto& layouts = shaderLayout.getDescriptorSetLayouts();
	if (layouts.empty()) {
		TOAST_CRITICAL("MeshPass", "ShaderLayout has no descriptor set layouts");
		return;
	}

	vk::DescriptorSetLayout setLayoutHandle = *layouts[0];
	m_descriptor_sets.clear();
	m_descriptor_sets.reserve(VulkanRenderer::kFramesInFlight);

	for (uint32_t i = 0; i < VulkanRenderer::kFramesInFlight; ++i) {
		vk::DescriptorSetAllocateInfo allocInfo(VulkanRenderer::instance->getDescriptorPoolHandle(), 1, &setLayoutHandle);
		auto desc = core.getDevice().allocateDescriptorSets(allocInfo);
		m_descriptor_sets.push_back(desc[0]);

		// Write the frame UBO into binding 0 for this descriptor set
		auto frameRes = VulkanRenderer::instance->getFrameUBORes(i);
		if (!frameRes->gpuBuffer.has_value()) {
			TOAST_CRITICAL("MeshPass", "Frame UBO buffer missing for frame {}", i);
		}
		vk::DescriptorBufferInfo bufferInfo(**frameRes->gpuBuffer, 0, sizeof(VulkanRenderer::FrameUBO));
		vk::WriteDescriptorSet write(m_descriptor_sets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo);

		// FIX: Changed nullptr to {} here as well to maintain consistency and safety
		core.getDevice().updateDescriptorSets(write, {});
	}
}

void MeshPass::updateUBO(uint32_t frameIndex) { }
}    // namespace toast::renderer
