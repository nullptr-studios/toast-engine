/// @file VulkanPipeline.hpp
/// @author dario
/// @date 16/05/2026.

#pragma once

#include "vulkan_common.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace toast::renderer {

class VulkanCore;

/**
 * @class VulkanPipeline
 * @brief Wraps Vulkan graphics and compute pipelines with shader compilation
 */
class VulkanPipeline {
public:
	enum class PipelineType : uint8_t {
		graphics,
		compute
	};

	struct Config {
		PipelineType pipeline_type = PipelineType::graphics;
		std::string debug_name;

		// Render state
		vk::Format color_format = vk::Format::eUndefined;
		std::optional<vk::Format> depth_format;
		vk::Extent2D extent;

		// Shader data TODO: Move this into own shader class
		std::vector<std::byte> shader_spirv;
		std::string vertex_entry = "vertexMain";
		std::string fragment_entry = "fragmentMain";
		std::string compute_entry = "computeMain";

		// Layouts are now provided from the outside
		vk::PipelineLayout pipeline_layout = nullptr;

		// Raster state
		vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
		vk::FrontFace front_face = vk::FrontFace::eCounterClockwise;    // Note: Counter-clockwise due to inverted projection matrix
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

	[[nodiscard]]
	auto getPipelineType() const -> PipelineType {
		return m_pipeline_type;
	}

private:
	std::optional<vk::raii::ShaderModule> m_shader_module;
	vk::raii::Pipeline m_pipeline = nullptr;
	PipelineType m_pipeline_type = PipelineType::graphics;
};

}    // namespace toast::renderer
