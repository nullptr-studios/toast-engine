/// @file VulkanPipeline.cpp
/// @author dario
/// @date 16/05/2026.

#include "vulkan_pipeline.hpp"

#include "toast/log.hpp"
#include "vulkan_core.hpp"

#include <array>
#include <stdexcept>

namespace toast::renderer {

namespace {
auto createShaderModule(const vk::raii::Device& device, std::span<const std::byte> spirv) -> vk::raii::ShaderModule {
	if (spirv.empty()) {
		TOAST_CRITICAL("VulkanPipeline", "Shader SPIR-V bytecode is empty!");
	}

	const auto byte_count = spirv.size_bytes();
	if ((byte_count % sizeof(uint32_t)) != 0) {
		TOAST_CRITICAL("VulkanPipeline", "Shader SPIR-V bytecode size is not 32-bit aligned!");
	}

	const auto* words = reinterpret_cast<const uint32_t*>(spirv.data());
	const vk::ShaderModuleCreateInfo shader_module_ci({}, byte_count, words);
	return {device, shader_module_ci};
}

// Dynamically creates the descriptor layout based on provided configuration bindings and optional binding flags
auto createDescriptorSetLayout(
    const vk::raii::Device& device, std::span<const vk::DescriptorSetLayoutBinding> bindings,
    std::span<const vk::DescriptorBindingFlags> binding_flags
) -> vk::raii::DescriptorSetLayout {
	if (!binding_flags.empty() && binding_flags.size() != bindings.size()) {
		TOAST_CRITICAL("VulkanPipeline", "Descriptor binding flags must match descriptor bindings count!");
	}

	vk::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags_ci {};
	if (!binding_flags.empty()) {
		binding_flags_ci =
		    vk::DescriptorSetLayoutBindingFlagsCreateInfo(static_cast<uint32_t>(binding_flags.size()), binding_flags.data());
	}

	vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_ci({}, static_cast<uint32_t>(bindings.size()), bindings.data());
	if (!binding_flags.empty()) {
		descriptor_set_layout_ci.pNext = &binding_flags_ci;
	}
	return {device, descriptor_set_layout_ci};
}

// Binds the descriptor sets layout and arbitrary push constants generically
auto createPipelineLayout(
    const vk::raii::Device& device, const vk::raii::DescriptorSetLayout& descriptor_set_layout,
    std::span<const vk::PushConstantRange> push_ranges
) -> vk::raii::PipelineLayout {
	const vk::DescriptorSetLayout raw_layout = *descriptor_set_layout;

	const vk::PipelineLayoutCreateInfo pipeline_layout_ci(
	    {}, 1, &raw_layout, static_cast<uint32_t>(push_ranges.size()), push_ranges.data()
	);
	return {device, pipeline_layout_ci};
}

auto createGraphicsPipelineImpl(
    const VulkanCore& core, const VulkanPipeline::Config& config, const vk::raii::ShaderModule& shader_module,
    const vk::raii::PipelineLayout& pipeline_layout
) -> vk::raii::Pipeline {
	const auto& device = core.getDevice();
	const std::array shader_stages = {
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *shader_module, config.vertex_entry.c_str()),
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *shader_module, config.fragment_entry.c_str())
	};

	const std::array color_attachment_formats {config.color_format};
	vk::PipelineRenderingCreateInfo rendering_ci {};
	rendering_ci.colorAttachmentCount = static_cast<uint32_t>(color_attachment_formats.size());
	rendering_ci.pColorAttachmentFormats = color_attachment_formats.data();
	if (config.depth_format.has_value()) {
		rendering_ci.depthAttachmentFormat = *config.depth_format;
	}

	const vk::PipelineVertexInputStateCreateInfo vertex_input_state_ci {};
	const vk::PipelineInputAssemblyStateCreateInfo input_assembly_ci({}, vk::PrimitiveTopology::eTriangleList);

	// Use dynamic viewport and scissor so the pipeline doesn't need to be rebuilt on window resize
	const vk::PipelineViewportStateCreateInfo viewport_state_ci({}, 1, nullptr, 1, nullptr);

	const std::array<vk::DynamicState, 2> dynamic_states {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	const vk::PipelineDynamicStateCreateInfo dynamic_state_ci(
	    {}, static_cast<uint32_t>(dynamic_states.size()), dynamic_states.data()
	);

	// Uses generic customizable flags passed from config definitions
	const vk::PipelineRasterizationStateCreateInfo rasterization_state_ci(
	    {}, false, false, vk::PolygonMode::eFill, config.cull_mode, config.front_face, false, 0.0f, 0.0f, 0.0f, 1.0f
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

	// Create depth/stencil state if depth format is specified
	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_ci {};
	if (config.depth_format.has_value()) {
		depth_stencil_state_ci = vk::PipelineDepthStencilStateCreateInfo(
		    {},
		    true,                    // depthTestEnable
		    true,                    // depthWriteEnable
		    vk::CompareOp::eLess,    // depthCompareOp
		    false,                   // depthBoundsTestEnable
		    false,                   // stencilTestEnable
		    vk::StencilOpState(),    // front
		    vk::StencilOpState(),    // back
		    0.0f,                    // minDepthBounds
		    1.0f                     // maxDepthBounds
		);
	}

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
	pipeline_ci.pDynamicState = &dynamic_state_ci;
	if (config.depth_format.has_value()) {
		pipeline_ci.pDepthStencilState = &depth_stencil_state_ci;
	}
	pipeline_ci.layout = *pipeline_layout;
	pipeline_ci.renderPass = nullptr;

	auto pipelines = device.createGraphicsPipelines(nullptr, pipeline_ci);
	return std::move(pipelines[0]);
}

auto createComputePipelineImpl(
    const VulkanCore& core, const VulkanPipeline::Config& config, const vk::raii::ShaderModule& shader_module,
    const vk::raii::PipelineLayout& pipeline_layout
) -> vk::raii::Pipeline {
	const auto& device = core.getDevice();
	const vk::PipelineShaderStageCreateInfo shader_stage_ci(
	    {}, vk::ShaderStageFlagBits::eCompute, *shader_module, config.compute_entry.c_str()
	);
	const vk::ComputePipelineCreateInfo pipeline_ci({}, shader_stage_ci, *pipeline_layout);
	auto pipelines = device.createComputePipelines(nullptr, pipeline_ci);
	return std::move(pipelines[0]);
}
}

VulkanPipeline::VulkanPipeline(const VulkanCore& core, const Config& config) {
	rebuild(core, config);
}

auto VulkanPipeline::rebuild(const VulkanCore& core, const Config& config) -> void {
	reset();
	m_pipeline_type = config.pipeline_type;

	if (config.depth_format.has_value() && *config.depth_format == vk::Format::eUndefined) {
		TOAST_CRITICAL("VulkanPipeline", "Pipeline depth format cannot be undefined!");
	}
	if (config.pipeline_type == PipelineType::graphics) {
		if (config.color_format == vk::Format::eUndefined) {
			TOAST_CRITICAL("VulkanPipeline", "Graphics pipeline requires a valid color format!");
		}
		if (config.extent.width == 0 || config.extent.height == 0) {
			TOAST_CRITICAL("VulkanPipeline", "Graphics pipeline requires a non-zero extent!");
		}
	}

	const auto& device = core.getDevice();

	m_shader_module.emplace(createShaderModule(device, config.shader_spirv));

	// Process descriptor and layout chains dynamically
	m_descriptor_set_layout = createDescriptorSetLayout(device, config.descriptor_bindings, config.descriptor_binding_flags);
	m_pipeline_layout = createPipelineLayout(device, m_descriptor_set_layout, config.push_constant_ranges);

	if (config.pipeline_type == PipelineType::graphics) {
		m_pipeline = createGraphicsPipelineImpl(core, config, *m_shader_module, m_pipeline_layout);
	} else {
		m_pipeline = createComputePipelineImpl(core, config, *m_shader_module, m_pipeline_layout);
	}
}

auto VulkanPipeline::reset() -> void {
	m_shader_module.reset();
	m_descriptor_set_layout = nullptr;
	m_pipeline_layout = nullptr;
	m_pipeline = nullptr;
}

}
