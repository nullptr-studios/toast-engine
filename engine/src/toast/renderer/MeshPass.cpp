/// @file MeshPass.cpp
/// @author dario
/// @date 08/06/2026.

#include "MeshPass.hpp"

#include "core/shader_compiler.hpp"
#include "core/vulkan_core.hpp"
#include "core/vulkan_renderer.hpp"
#include "toast/log.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

namespace toast::renderer {
MeshPass::MeshPass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent) {
	auto shaderSpirv = toast::renderer::ShaderCompiler::compileShaderModuleFromSource("./mesh.slang");

	shaderLayout.rebuild(core, shaderSpirv.program->getLayout());

	toast::renderer::VulkanPipeline::Config config {
	  .pipeline_type = toast::renderer::VulkanPipeline::PipelineType::graphics,
	  .color_format = colorFormat,
	  .depth_format = depthFormat,
	  .extent = extent,
	  .shader_spirv = std::move(shaderSpirv.spirv),
	  .pipeline_layout = shaderLayout.getPipelineLayout()

	};

	m_pipeline.rebuild(core, config);

	createResources(core);
}

void MeshPass::update(uint32_t frame_index, float dt) {
	updateUBO(frame_index);
}

void MeshPass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
	(void)imageIndex;
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	const FrameResources* frameUBO = toast::renderer::VulkanRenderer::instance->getFrameUBORes(frameIndex);
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *shaderLayout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*frameUBO->descriptorSet},
	    {}
	);

	const auto* frame = toast::renderer::VulkanRenderer::instance->renderingFrame();
	if (frame == nullptr) {
		return;
	}

	for (const auto& proxy : frame->mesh_instances) {
		if (proxy.mesh == nullptr || !proxy.mesh->isReady()) {
			continue;
		}

		DrawPushConstants data {};
		data.model = proxy.model;

		cmd.pushConstants(
		    shaderLayout.getPipelineLayout(),
		    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
		    0,
		    sizeof(DrawPushConstants),
		    &data
		);

		proxy.mesh->bind(cmd);
		proxy.mesh->draw(cmd);
	}
}

void MeshPass::createResources(const toast::renderer::VulkanCore& core) { }

void MeshPass::updateUBO(uint32_t frameIndex) { }
}
