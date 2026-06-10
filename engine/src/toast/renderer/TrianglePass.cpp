/// @file TrianglePass.cpp
/// @author dario
/// @date 07/06/2026.

#include "TrianglePass.hpp"

#include "ShaderCompiler.hpp"
#include "ShaderLayout.hpp"
#include "VulkanCore.hpp"
#include "VulkanRenderer.hpp"
#include "toast/log.hpp"

TrianglePass::TrianglePass(
    const toast::renderer::VulkanCore& core, vk::Format colorFormat, vk::Format depthFormat, vk::Extent2D extent
) {
	auto shaderSpirv = toast::renderer::ShaderCompiler::compileShaderModuleFromSource("./mirrors.slang");

	m_shaderlayout.rebuild(core, shaderSpirv.program->getLayout());

	toast::renderer::VulkanPipeline::Config config {
	  .pipeline_type = toast::renderer::VulkanPipeline::PipelineType::Graphics,
	  .color_format = colorFormat,
	  .depth_format = depthFormat,
	  .extent = extent,
	  .shader_spirv = std::move(shaderSpirv.spirv),
	  .pipeline_layout = m_shaderlayout.getPipelineLayout()

	};

	m_uniformsData.resize(toast::renderer::VulkanRenderer::kFramesInFlight);
	m_uniformsRes.resize(toast::renderer::VulkanRenderer::kFramesInFlight);

	m_pipeline.rebuild(core, config);

	createResources(core);
}

void TrianglePass::update(uint32_t frame_index, float dt) {
	totalTime += dt;

	m_uniformsData[frame_index].iTime = totalTime;

	updateFrameUniformData(frame_index);
}

void TrianglePass::record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t imageIndex) {
	// bind
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.getPipeline());

	// bind descriptor sets
	auto& frame = m_uniformsRes[frameIndex];
	cmd.bindDescriptorSets(
	    vk::PipelineBindPoint::eGraphics,
	    *m_shaderlayout.getPipelineLayout(),
	    0,
	    std::array<vk::DescriptorSet, 1> {*frame.descriptorSet},
	    {}
	);
	// Draw fullscreen
	cmd.draw(6, 1, 0, 0);
}

void TrianglePass::createResources(const toast::renderer::VulkanCore& core) {
	const auto& device = core.getDevice();

	const vk::DeviceSize bufferSize = sizeof(FrameUniformData);

	vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, toast::renderer::VulkanRenderer::kFramesInFlight);

	vk::DescriptorPoolCreateInfo poolCI {};
	poolCI.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;    // self frees are valid
	poolCI.maxSets = toast::renderer::VulkanRenderer::kFramesInFlight;
	poolCI.poolSizeCount = 1;
	poolCI.pPoolSizes = &poolSize;

	m_descriptorPool = vk::raii::DescriptorPool(device, poolCI);

	const auto& layouts = m_shaderlayout.getDescriptorSetLayouts();

	if (layouts.empty()) {
		TOAST_CRITICAL("TrianglePass", "No descriptor layouts found");
	}

	const vk::DescriptorSetLayout descriptorLayout = *layouts[0];

	for (auto& frame : m_uniformsRes) {
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

auto TrianglePass::updateFrameUniformData(uint32_t frame_index) -> void {
	// hardcoded for now
	m_uniformsData[frame_index].iResolution[0] = static_cast<float>(1080);
	m_uniformsData[frame_index].iResolution[1] = static_cast<float>(720);

	auto& allocation = m_uniformsRes[frame_index].gpuBuffer->getAllocation();
	// With VMA_ALLOCATION_CREATE_MAPPED
	auto* mapped = allocation.getInfo().pMappedData;

	if (mapped) {
		std::memcpy(mapped, &m_uniformsData[frame_index], sizeof(FrameUniformData));

		allocation.flush(0, sizeof(FrameUniformData));
	}
}
