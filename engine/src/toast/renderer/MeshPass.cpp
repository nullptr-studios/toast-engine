/// @file MeshPass.cpp
/// @author dario
/// @date 08/06/2026.

#include "MeshPass.hpp"

#include "ShaderCompiler.hpp"
#include "VulkanCore.hpp"
#include "VulkanRenderer.hpp"
#include "toast/log.hpp"

#include <glm/gtc/matrix_transform.hpp>

MeshPass::MeshPass(const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent) {
	auto shaderSpirv = toast::renderer::ShaderCompiler::compileShaderModuleFromSource("./mesh.slang");

	shaderLayout.rebuild(core, shaderSpirv.program->getLayout());

	toast::renderer::VulkanPipeline::Config config {
	  .pipeline_type = toast::renderer::VulkanPipeline::PipelineType::Graphics,
	  .color_format = colorFormat,
	  .depth_format = depthFormat,
	  .extent = extent,
	  .shader_spirv = std::move(shaderSpirv.spirv),
	  .pipeline_layout = shaderLayout.getPipelineLayout()

	};

	cameraUBO.resize(toast::renderer::VulkanRenderer::kFramesInFlight);
	cameraUBORes.resize(toast::renderer::VulkanRenderer::kFramesInFlight);

	m_pipeline.rebuild(core, config);

	createResources(core);

	// create Mesh
	toast::renderer::VulkanMesh::UploadData data {};
	std::vector<toast::renderer::Vertex> vertices = {
	  {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

	  { {0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

	  {  {0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

	  { {-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}
	};

	std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

	data.indices = indices;
	data.vertices = vertices;
	// mesh.create(core, data, core.getGraphicsQueueFamilyIndex(), core.getTransferQueueFamilyIndex());
	toast::renderer::VulkanRenderer::instance->queueMeshUpload(mesh, data);
}

void MeshPass::update(uint32_t frame_index, float dt) {
	updateUBO(frame_index);
}

void MeshPass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
	DrawPushConstants data {};
	data.model = glm::identity<glm::mat4>();

	mesh.bind(cmd);

	cmd.pushConstants(shaderLayout.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(DrawPushConstants), &data);

	auto& frame = cameraUBORes[frameIndex];
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *shaderLayout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*frame.descriptorSet},
	    {}
	);

	mesh.draw(cmd);
}

void MeshPass::createResources(const toast::renderer::VulkanCore& core) {
	const auto& device = core.getDevice();

	const vk::DeviceSize bufferSize = sizeof(CameraUBO);

	vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, toast::renderer::VulkanRenderer::kFramesInFlight);

	vk::DescriptorPoolCreateInfo poolCI {};
	poolCI.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;    // self frees are valid
	poolCI.maxSets = toast::renderer::VulkanRenderer::kFramesInFlight;
	poolCI.poolSizeCount = 1;
	poolCI.pPoolSizes = &poolSize;

	m_descriptorPool = vk::raii::DescriptorPool(device, poolCI);

	const auto& layouts = shaderLayout.getDescriptorSetLayouts();

	if (layouts.empty()) {
		TOAST_CRITICAL("MeshPass", "No descriptor layouts found");
	}

	const vk::DescriptorSetLayout descriptorLayout = *layouts[0];

	for (auto& frame : cameraUBORes) {
		// create ubo
		vk::BufferCreateInfo bufferCI {};
		bufferCI.size = bufferSize;
		bufferCI.usage = vk::BufferUsageFlagBits::eUniformBuffer;

		// allocation
		vma::AllocationCreateInfo allocCI {};
		allocCI.usage = vma::MemoryUsage::eAutoPreferHost;

		allocCI.flags = vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite;

		// create buffer
		frame.gpuBuffer.emplace(core.getAllocator().createBuffer(bufferCI, allocCI));

		// no staging buffer

		// allocate descriptor
		auto descriptorSets = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo(*m_descriptorPool, 1, &descriptorLayout));

		frame.descriptorSet = std::move(descriptorSets[0]);
		// write descriptor
		vk::DescriptorBufferInfo bufferInfo(**frame.gpuBuffer, 0, bufferSize);

		vk::WriteDescriptorSet write(frame.descriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo);

		device.updateDescriptorSets(write, nullptr);
	}
}

void MeshPass::updateUBO(uint32_t frameIndex) {
	cameraUBO[frameIndex].projection = glm::perspective(90.f, 16.f / 9.f, 0.1f, 100.f);
	cameraUBO[frameIndex].view = glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	cameraUBO[frameIndex].viewProjection = cameraUBO[frameIndex].view * cameraUBO[frameIndex].projection;

	auto& allocation = cameraUBORes[frameIndex].gpuBuffer->getAllocation();
	// With VMA_ALLOCATION_CREATE_MAPPED
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &cameraUBO[frameIndex], sizeof(CameraUBO));

		allocation.flush(0, sizeof(CameraUBO));
	}
}
