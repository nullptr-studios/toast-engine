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
	struct Config {
		std::string debug_name;
		vk::Format color_format = vk::Format::eUndefined;
		vk::Extent2D extent {};
		std::vector<std::byte> shader_spirv;
		std::string vertex_entry = "vertexMain";
		std::string fragment_entry = "fragmentMain";
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

private:
	std::optional<vk::raii::ShaderModule> m_shader_module;
	vk::raii::DescriptorSetLayout m_descriptor_set_layout = nullptr;
	vk::raii::PipelineLayout m_pipeline_layout = nullptr;
	vk::raii::Pipeline m_pipeline = nullptr;
};

}
