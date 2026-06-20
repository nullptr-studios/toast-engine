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

class VulkanPipeline {
public:
	enum class PipelineType : uint8_t {
		graphics,
		compute
	};

	struct Config {
		PipelineType pipeline_type = PipelineType::graphics;
		std::string debug_name;
		vk::Format color_format = vk::Format::eUndefined;
		vk::Extent2D extent {};
		std::vector<std::byte> shader_spirv;
		std::optional<vk::Format> depth_format;
		std::string vertex_entry = "vertexMain";
		std::string fragment_entry = "fragmentMain";
		std::string compute_entry = "computeMain";

		std::vector<vk::DescriptorSetLayoutBinding> descriptor_bindings;
		std::vector<vk::DescriptorBindingFlags> descriptor_binding_flags;
		std::vector<vk::PushConstantRange> push_constant_ranges;

		vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eBack;
		vk::FrontFace front_face = vk::FrontFace::eClockwise;
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
		return m_pipeline != nullptr && m_pipeline_layout != nullptr;
	}

	[[nodiscard]]
	auto getPipeline() const -> const vk::raii::Pipeline& {
		return m_pipeline;
	}

	[[nodiscard]]
	auto getPipelineLayout() const -> const vk::raii::PipelineLayout& {
		return m_pipeline_layout;
	}

	[[nodiscard]]
	auto getDescriptorSetLayout() const -> const vk::raii::DescriptorSetLayout& {
		return m_descriptor_set_layout;
	}

	[[nodiscard]]
	auto getPipelineType() const -> PipelineType {
		return m_pipeline_type;
	}

private:
	std::optional<vk::raii::ShaderModule> m_shader_module;
	vk::raii::DescriptorSetLayout m_descriptor_set_layout = nullptr;
	vk::raii::PipelineLayout m_pipeline_layout = nullptr;
	vk::raii::Pipeline m_pipeline = nullptr;
	PipelineType m_pipeline_type = PipelineType::graphics;
};

}    // namespace toast::renderer
