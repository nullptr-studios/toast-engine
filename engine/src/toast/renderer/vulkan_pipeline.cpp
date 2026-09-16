/// @file VulkanPipeline.cpp
/// @author dario
/// @date 16/05/2026

#include "vulkan_pipeline.hpp"

#include "spirv_entry_points.hpp"
#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace renderer {

namespace {
auto createShaderModule(const vk::raii::Device& device, std::span<const std::byte> spirv) -> vk::raii::ShaderModule {
	if (spirv.empty()) {
		TOAST_CRITICAL("Render", "Shader SPIR-V bytecode is empty!");
	}

	const auto byte_count = spirv.size_bytes();
	if ((byte_count % sizeof(uint32_t)) != 0) {
		TOAST_CRITICAL("Render", "Shader SPIR-V bytecode size is not 32-bit aligned!");
	}

	const auto* words = reinterpret_cast<const uint32_t*>(spirv.data());
	const vk::ShaderModuleCreateInfo shader_module_ci({}, byte_count, words);
	return {device, shader_module_ci};
}

auto createGraphicsPipelineImpl(
    const VulkanCore& core, const VulkanPipeline::Config& config, const vk::raii::ShaderModule& shader_module,
    const vk::PipelineLayout& pipeline_layout
) -> vk::raii::Pipeline {
	ZoneScoped;
	const auto& device = core.getDevice();

	// The create infos point into these strings
	const std::string vertex_entry =
	    spirv::resolveEntryPoint(config.shader_spirv, spirv::ExecutionModel::vertex, config.vertex_entry, config.debug_name);
	const std::string fragment_entry =
	    config.depth_only ? config.fragment_entry
	                      : spirv::resolveEntryPoint(
	                            config.shader_spirv, spirv::ExecutionModel::fragment, config.fragment_entry, config.debug_name
	                        );

	const std::array shader_stages = {
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *shader_module, vertex_entry.c_str()),
	  vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *shader_module, fragment_entry.c_str())
	};
	const uint32_t stage_count = config.depth_only ? 1u : static_cast<uint32_t>(shader_stages.size());

	std::vector<vk::Format> color_attachment_formats {config.color_format};
	color_attachment_formats.insert(
	    color_attachment_formats.end(), config.extra_color_formats.begin(), config.extra_color_formats.end()
	);
	vk::PipelineRenderingCreateInfo rendering_ci {};
	rendering_ci.colorAttachmentCount = config.depth_only ? 0u : static_cast<uint32_t>(color_attachment_formats.size());
	rendering_ci.pColorAttachmentFormats = config.depth_only ? nullptr : color_attachment_formats.data();
	if (config.depth_format.has_value()) {
		rendering_ci.depthAttachmentFormat = *config.depth_format;
	}
	rendering_ci.viewMask = config.view_mask;

	const uint32_t vertex_binding_count =
	    config.vertex_attributes.empty() ? 0 : static_cast<uint32_t>(config.vertex_bindings.size());
	const vk::PipelineVertexInputStateCreateInfo vertex_input_ci(
	    {},
	    vertex_binding_count,
	    config.vertex_bindings.data(),
	    static_cast<uint32_t>(config.vertex_attributes.size()),
	    config.vertex_attributes.data()
	);
	const vk::PipelineInputAssemblyStateCreateInfo input_assembly_ci({}, config.topology);

	const vk::PipelineViewportStateCreateInfo viewport_state_ci({}, 1, nullptr, 1, nullptr);

	const std::array<vk::DynamicState, 2> dynamic_states {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	const vk::PipelineDynamicStateCreateInfo dynamic_state_ci(
	    {}, static_cast<uint32_t>(dynamic_states.size()), dynamic_states.data()
	);

	const bool depth_bias_enable = config.depth_bias_constant != 0.0f || config.depth_bias_slope != 0.0f;
	const vk::PipelineRasterizationStateCreateInfo rasterization_state_ci(
	    {},
	    false,
	    false,
	    vk::PolygonMode::eFill,
	    config.cull_mode,
	    config.front_face,
	    depth_bias_enable,
	    config.depth_bias_constant,
	    0.0f,
	    config.depth_bias_slope,
	    1.0f
	);

	const vk::PipelineMultisampleStateCreateInfo multisample_state_ci({}, vk::SampleCountFlagBits::e1);

	using BlendPreset = VulkanPipeline::BlendPreset;
	auto blend_preset = config.blend_preset;
	if (blend_preset == BlendPreset::none && config.blend_enable) {
		blend_preset = BlendPreset::alpha;
	}

	vk::BlendFactor src_factor = vk::BlendFactor::eOne;
	vk::BlendFactor dst_factor = vk::BlendFactor::eZero;
	switch (blend_preset) {
		case BlendPreset::alpha:
			src_factor = vk::BlendFactor::eSrcAlpha;
			dst_factor = vk::BlendFactor::eOneMinusSrcAlpha;
			break;
		case BlendPreset::premultiplied:
			src_factor = vk::BlendFactor::eOne;
			dst_factor = vk::BlendFactor::eOneMinusSrcAlpha;
			break;
		case BlendPreset::additive:
			src_factor = vk::BlendFactor::eOne;
			dst_factor = vk::BlendFactor::eOne;
			break;
		case BlendPreset::multiply:
			src_factor = vk::BlendFactor::eDstColor;
			dst_factor = vk::BlendFactor::eZero;
			break;
		case BlendPreset::none: break;
	}

	const vk::ColorComponentFlags rgb_write =
	    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;

	std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments;
	color_blend_attachments.emplace_back(
	    blend_preset != BlendPreset::none,
	    src_factor,
	    dst_factor,
	    vk::BlendOp::eAdd,
	    vk::BlendFactor::eOne,
	    vk::BlendFactor::eZero,
	    vk::BlendOp::eAdd,
	    rgb_write
	);

	for ([[maybe_unused]]
	     const vk::Format extra : config.extra_color_formats) {
		color_blend_attachments.emplace_back(
		    false,
		    vk::BlendFactor::eOne,
		    vk::BlendFactor::eZero,
		    vk::BlendOp::eAdd,
		    vk::BlendFactor::eOne,
		    vk::BlendFactor::eZero,
		    vk::BlendOp::eAdd,
		    config.write_extra_color ? (rgb_write | vk::ColorComponentFlagBits::eA) : vk::ColorComponentFlags {}
		);
	}

	const vk::PipelineColorBlendStateCreateInfo color_blend_state_ci(
	    {},
	    false,
	    vk::LogicOp::eCopy,
	    config.depth_only ? 0u : static_cast<uint32_t>(color_blend_attachments.size()),
	    config.depth_only ? nullptr : color_blend_attachments.data(),
	    std::array {0.0f, 0.0f, 0.0f, 0.0f}
	);

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state_ci {};
	if (config.depth_format.has_value()) {
		depth_stencil_state_ci = vk::PipelineDepthStencilStateCreateInfo(
		    {},
		    config.depth_test,
		    config.depth_write,
		    config.depth_compare,    // depthCompareOp
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
	pipeline_ci.stageCount = stage_count;
	pipeline_ci.pStages = shader_stages.data();
	pipeline_ci.pVertexInputState = &vertex_input_ci;
	pipeline_ci.pInputAssemblyState = &input_assembly_ci;
	pipeline_ci.pViewportState = &viewport_state_ci;
	pipeline_ci.pRasterizationState = &rasterization_state_ci;
	pipeline_ci.pMultisampleState = &multisample_state_ci;
	pipeline_ci.pColorBlendState = &color_blend_state_ci;
	pipeline_ci.pDynamicState = &dynamic_state_ci;
	if (config.depth_format.has_value()) {
		pipeline_ci.pDepthStencilState = &depth_stencil_state_ci;
	}
	pipeline_ci.layout = pipeline_layout;
	pipeline_ci.renderPass = nullptr;

	auto pipelines = device.createGraphicsPipelines(nullptr, pipeline_ci);
	return std::move(pipelines[0]);
}

auto createComputePipelineImpl(
    const VulkanCore& core, const VulkanPipeline::Config& config, const vk::raii::ShaderModule& shader_module,
    const vk::PipelineLayout& pipeline_layout
) -> vk::raii::Pipeline {
	ZoneScoped;
	const auto& device = core.getDevice();
	const std::string compute_entry =
	    spirv::resolveEntryPoint(config.shader_spirv, spirv::ExecutionModel::compute, config.compute_entry, config.debug_name);
	const vk::PipelineShaderStageCreateInfo shader_stage_ci(
	    {}, vk::ShaderStageFlagBits::eCompute, *shader_module, compute_entry.c_str()
	);

	const vk::ComputePipelineCreateInfo pipeline_ci({}, shader_stage_ci, pipeline_layout);
	auto pipelines = device.createComputePipelines(nullptr, pipeline_ci);
	return std::move(pipelines[0]);
}
}

VulkanPipeline::VulkanPipeline(const VulkanCore& core, const Config& config) {
	rebuild(core, config);
}

auto VulkanPipeline::rebuild(const VulkanCore& core, const Config& config) -> void {
	ZoneScoped;
	reset();

	if (!config.pipeline_layout) {
		TOAST_CRITICAL("Render", "A valid pipeline_layout must be provided!");
	}

	if (config.depth_format.has_value() && *config.depth_format == vk::Format::eUndefined) {
		TOAST_CRITICAL("Render", "Pipeline depth format cannot be undefined!");
	}
	if (config.pipeline_type == PipelineType::graphics) {
		if (config.color_format == vk::Format::eUndefined && !config.depth_only) {
			TOAST_CRITICAL("Render", "Graphics pipeline requires a valid color format!");
		}
		if (config.depth_only && !config.depth_format.has_value()) {
			TOAST_CRITICAL("Render", "Depth-only graphics pipeline requires a depth format!");
		}
		if (config.extent.width == 0 || config.extent.height == 0) {
			TOAST_CRITICAL("Render", "Graphics pipeline requires a non-zero extent!");
		}
	}

	const auto& device = core.getDevice();
	m_shader_module.emplace(createShaderModule(device, config.shader_spirv));

	if (config.pipeline_type == PipelineType::graphics) {
		m_pipeline = createGraphicsPipelineImpl(core, config, *m_shader_module, config.pipeline_layout);
	} else {
		m_pipeline = createComputePipelineImpl(core, config, *m_shader_module, config.pipeline_layout);
	}

	if (!config.debug_name.empty()) {
		setDebugName(core, **m_shader_module, config.debug_name + " ShaderModule");
		setDebugName(core, *m_pipeline, config.debug_name + " Pipeline");
	}
}

auto VulkanPipeline::reset() -> void {
	m_shader_module.reset();
	m_pipeline = nullptr;
}
}
