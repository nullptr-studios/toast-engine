/// @file VulkanPipeline.hpp
/// @author dario
/// @date 16/05/2026

#pragma once

#include "depth_convention.hpp"
#include "vulkan_common.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace renderer {

class VulkanCore;

class VulkanPipeline {
public:
	enum class PipelineType : uint8_t {
		graphics,
		compute
	};

	enum class BlendPreset : uint8_t {
		none,
		alpha,            // srcAlpha 1-srcAlpha
		premultiplied,    // one 1-srcAlpha
		additive,         // one one
		multiply,         // dstColor zero
		dual_source,      // one src1Color and needs dualSrcBlend
	};

	struct Config {
		PipelineType pipeline_type = PipelineType::graphics;
		std::string debug_name;

		vk::Format color_format = vk::Format::eUndefined;
		std::optional<vk::Format> depth_format;
		/// Without depth_format the depth aspect is left out of the rendering
		std::optional<vk::Format> stencil_format;
		vk::Extent2D extent;

		std::vector<vk::Format> extra_color_formats;

		bool write_extra_color = false;

		/// Bit i blends extra attachment i with blend_preset and never for an integer format
		uint32_t extra_blend_mask = 0;

		/// color_format keeps its destination alpha unless set
		bool write_color_alpha = false;

		/// False masks color_format off entirely while extra attachments still follow write_extra_color
		bool write_color = true;

		bool depth_only = false;

		/// With depth_only keeps the fragment stage for shaders that write SV_Depth
		bool depth_fragment = false;

		/// Must equal every RenderingInfo viewMask. 0 is not multiview and leaves SV_ViewID undefined
		uint32_t view_mask = 0;

		// TODO move into its own shader class
		std::vector<std::byte> shader_spirv;
		std::string vertex_entry = "vertexMain";
		/// Set for a task and mesh pipeline which has no vertex stage or vertex input
		std::string mesh_entry;
		/// Only with mesh_entry
		std::string task_entry;
		/// Task stage module since Slang gives task and mesh entry points of one module separate payload variables that
		/// NVIDIA drivers mishandle (shader-slang/slang#10123)
		std::vector<std::byte> task_spirv;
		std::string fragment_entry = "fragmentMain";
		std::string compute_entry = "computeMain";

		vk::PipelineLayout pipeline_layout = nullptr;

		std::vector<vk::VertexInputBindingDescription> vertex_bindings;
		std::vector<vk::VertexInputAttributeDescription> vertex_attributes;

		vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

		vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
		vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;    // Counter clockwise due to the inverted projection matrix

		bool depth_test = true;
		bool depth_write = true;
		/// Scene convention. Shadow maps set eLess and far plane draws need depth::k_closer_or_equal
		vk::CompareOp depth_compare = depth::k_closer;

		/// Every surviving fragment writes the dynamic stencil reference through the dynamic write mask
		bool stencil_replace = false;

		/// Clamps depth instead of clipping so casters in front of a shadow view still occlude
		bool depth_clamp = false;

		float depth_bias_constant = 0.0f;
		float depth_bias_slope = 0.0f;
		bool blend_enable = false;
		BlendPreset blend_preset = BlendPreset::none;
	};

	VulkanPipeline() = default;
	explicit VulkanPipeline(const VulkanCore& core, const Config& config);
	~VulkanPipeline() = default;

	VulkanPipeline(const VulkanPipeline&) = delete;
	auto operator=(const VulkanPipeline&) -> VulkanPipeline& = delete;
	VulkanPipeline(VulkanPipeline&&) = delete;
	auto operator=(VulkanPipeline&&) -> VulkanPipeline& = delete;

	auto rebuild(const VulkanCore& core, const Config& config) -> void;
	auto reset() -> void;

	[[nodiscard]]
	auto isReady() const -> bool {
		return m_pipeline != nullptr;
	}

	[[nodiscard]]
	auto getPipeline() const -> const vk::raii::Pipeline& {
		return m_pipeline;
	}

private:
	std::optional<vk::raii::ShaderModule> m_shader_module;
	std::optional<vk::raii::ShaderModule> m_task_module;
	vk::raii::Pipeline m_pipeline = nullptr;
};

}    // namespace renderer
