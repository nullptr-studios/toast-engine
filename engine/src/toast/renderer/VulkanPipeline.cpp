/// @file VulkanPipeline.cpp
/// @author dario
/// @date 16/05/2026.

#include "VulkanPipeline.hpp"

#include "VulkanCore.hpp"
#include "toast/log.hpp"

#include <array>
#include <stdexcept>

namespace toast::renderer {

namespace {
auto createShaderModule(const vk::raii::Device& device, std::span<const std::byte> spirv) -> vk::raii::ShaderModule {
	if (spirv.empty()) {
		TOAST_CRITICAL("VulkanPipeline", "Toast Engine Error: Shader SPIR-V bytecode is empty!");
	}

	const auto byte_count = spirv.size_bytes();
	if ((byte_count % sizeof(uint32_t)) != 0) {
		TOAST_CRITICAL("VulkanPipeline", "Toast Engine Error: Shader SPIR-V bytecode size is not 32-bit aligned!");
	}

	const auto* words = reinterpret_cast<const uint32_t*>(spirv.data());
	// vk::ShaderModuleCreateInfo expects the shader code size in bytes
	const vk::ShaderModuleCreateInfo shader_module_ci({}, byte_count, words);
	return {device, shader_module_ci};
}

auto createDescriptorSetLayout(const vk::raii::Device& device) -> vk::raii::DescriptorSetLayout {
	const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_ci {};
	return {device, descriptor_set_layout_ci};
}

auto createPipelineLayout(const vk::raii::Device& device, const vk::raii::DescriptorSetLayout& descriptor_set_layout)
    -> vk::raii::PipelineLayout {
	const vk::PipelineLayoutCreateInfo pipeline_layout_ci({}, 1, &*descriptor_set_layout);
	return {device, pipeline_layout_ci};
}

auto createGraphicsPipeline(
    const vk::raii::Device& device, const vk::raii::PipelineLayout& pipeline_layout, vk::Extent2D extent,
    const vk::raii::ShaderModule& shader_module, const std::string& vertex_entry, const std::string& fragment_entry,
    vk::Format color_format
) -> vk::raii::Pipeline {
	const std::array shader_stages = {
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *shader_module, vertex_entry.c_str()),
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *shader_module, fragment_entry.c_str())
	};
	const std::array color_attachment_formats {color_format};
	vk::PipelineRenderingCreateInfo rendering_ci {};
	rendering_ci.colorAttachmentCount = static_cast<uint32_t>(color_attachment_formats.size());
	rendering_ci.pColorAttachmentFormats = color_attachment_formats.data();

	const vk::PipelineVertexInputStateCreateInfo vertex_input_state_ci {};
	const vk::PipelineInputAssemblyStateCreateInfo input_assembly_ci({}, vk::PrimitiveTopology::eTriangleList);
	const vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f);
	const vk::Rect2D scissor({0, 0}, extent);
	const vk::PipelineViewportStateCreateInfo viewport_state_ci({}, 1, &viewport, 1, &scissor);
	const vk::PipelineRasterizationStateCreateInfo rasterization_state_ci(
	    {},
	    false,
	    false,
	    vk::PolygonMode::eFill,
	    vk::CullModeFlagBits::eBack,
	    vk::FrontFace::eCounterClockwise,
	    false,
	    0.0f,
	    0.0f,
	    0.0f,
	    1.0f
	);
	const vk::PipelineMultisampleStateCreateInfo multisample_state_ci({}, vk::SampleCountFlagBits::e1);
	const vk::PipelineColorBlendAttachmentState color_blend_attachment(
	    false,
	    vk::BlendFactor::eOne,
	    vk::BlendFactor::eZero,
	    vk::BlendOp::eAdd,
	    vk::BlendFactor::eOne,
	    vk::BlendFactor::eZero,
	    vk::BlendOp::eAdd,
	    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
	        vk::ColorComponentFlagBits::eA
	);
	const vk::PipelineColorBlendStateCreateInfo color_blend_state_ci(
	    {}, false, vk::LogicOp::eCopy, 1, &color_blend_attachment, std::array {0.0f, 0.0f, 0.0f, 0.0f}
	);

	vk::GraphicsPipelineCreateInfo pipeline_ci {};
	pipeline_ci.pNext = &rendering_ci;
	pipeline_ci.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_ci.pStages = shader_stages.data();
	pipeline_ci.pVertexInputState = &vertex_input_state_ci;
	pipeline_ci.pInputAssemblyState = &input_assembly_ci;
	pipeline_ci.pViewportState = &viewport_state_ci;
	pipeline_ci.pRasterizationState = &rasterization_state_ci;
	pipeline_ci.pMultisampleState = &multisample_state_ci;
	pipeline_ci.pColorBlendState = &color_blend_state_ci;
	pipeline_ci.layout = *pipeline_layout;
	pipeline_ci.renderPass = nullptr;

	auto pipelines = device.createGraphicsPipelines(nullptr, pipeline_ci);
	return std::move(pipelines[0]);
}
}    // namespace

VulkanPipeline::VulkanPipeline(const VulkanCore& core, const Config& config) {
	TOAST_INFO("VulkanPipeline", "Creating pipeline");
	rebuild(core, config);
}

auto VulkanPipeline::rebuild(const VulkanCore& core, const Config& config) -> void {
	TOAST_INFO("VulkanPipeline", "Rebuilding pipeline");
	reset();

	if (config.color_format == vk::Format::eUndefined) {
		throw std::runtime_error("Toast Engine Error: Pipeline requires a valid color format!");
	}
	if (config.extent.width == 0 || config.extent.height == 0) {
		throw std::runtime_error("Toast Engine Error: Pipeline requires a non-zero extent!");
	}

	const auto& device = core.getDevice();
	if (!config.debug_name.empty()) {
		TOAST_INFO("VulkanPipeline", "Pipeline name: {}", config.debug_name);
	}

	m_shader_module.emplace(createShaderModule(device, config.shader_spirv));

	TOAST_INFO("VulkanPipeline", "Shader modules created");

	m_descriptor_set_layout = createDescriptorSetLayout(device);
	m_pipeline_layout = createPipelineLayout(device, m_descriptor_set_layout);

	TOAST_INFO("VulkanPipeline", "Pipeline layout created");

	m_pipeline = createGraphicsPipeline(
	    device, m_pipeline_layout, config.extent, *m_shader_module, config.vertex_entry, config.fragment_entry, config.color_format
	);

	TOAST_INFO("VulkanPipeline", "Graphics pipeline created successfully");
}

auto VulkanPipeline::reset() -> void {
	TOAST_INFO("VulkanPipeline", "Resetting pipeline state");
	m_shader_module.reset();
	m_descriptor_set_layout = nullptr;
	m_pipeline_layout = nullptr;
	m_pipeline = nullptr;
}

}
