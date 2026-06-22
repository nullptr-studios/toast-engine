/// @file MeshPass.cpp
/// @author dario
/// @date 08/06/2026.

#include "MeshPass.hpp"

#include "core/shader_compiler.hpp"
#include "core/vulkan_core.hpp"
#include "core/vulkan_renderer.hpp"
#include "gizmo.hpp"
#include "toast/log.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

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

	// create Mesh
	toast::renderer::VulkanMesh::UploadData data {};

	data.indices = GizmoIndices;
	data.vertices = GizmoVertex;
	// mesh.create(core, data, core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex());
	toast::renderer::VulkanRenderer::instance->queueMeshUpload(mesh, data);
}

void MeshPass::update(uint32_t frame_index, float dt) {
	updateUBO(frame_index);
}

void MeshPass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
	DrawPushConstants data {};
	data.model = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, 1));

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	// TODO: Gizmo model coordinate system needs Y-up conversion
	mesh.bind(cmd);

	// Bind the frame data UBO

	const FrameResources* frameUBO = toast::renderer::VulkanRenderer::instance->getFrameUBORes(frameIndex);
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *shaderLayout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*frameUBO->descriptorSet},
	    {}
	);

	// TODO: Improve reflection system to automatically determine required shader stages
	cmd.pushConstants(
	    shaderLayout.getPipelineLayout(),
	    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	    0,
	    sizeof(DrawPushConstants),
	    &data
	);

	mesh.draw(cmd);
}

void MeshPass::createResources(const toast::renderer::VulkanCore& core) { }

void MeshPass::updateUBO(uint32_t frameIndex) { }
}
