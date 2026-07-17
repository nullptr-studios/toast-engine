#pragma once

#include "shader_reflection.hpp"

#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {
class VulkanCore;

/**
 * @class ShaderLayout
 * @brief Constructs pipeline layouts and descriptor sets from shader reflection data
 */
class ShaderLayout {
public:
	ShaderLayout() = default;
	~ShaderLayout() = default;

	/**
	 * @brief Rebuilds descriptor set layouts, push constant ranges and the
	 *        pipeline layout from plain reflection data
	 * @param debug_name Name used for the Vulkan debug labels of the created objects
	 */
	void rebuild(const VulkanCore& core, const ShaderReflection& reflection, std::string_view debug_name);

	[[nodiscard]]
	auto getPipelineLayout() const -> const vk::raii::PipelineLayout& {
		return m_pipeline_layout;
	}

	[[nodiscard]]
	auto getDescriptorSetLayouts() const -> const std::vector<vk::raii::DescriptorSetLayout>& {
		return m_descriptor_set_layouts;
	}

	[[nodiscard]]
	auto getPushConstantRanges() const -> const std::vector<vk::PushConstantRange>& {
		return m_push_constant_ranges;
	}

private:
	std::vector<vk::raii::DescriptorSetLayout> m_descriptor_set_layouts;
	vk::raii::PipelineLayout m_pipeline_layout = nullptr;
	std::vector<vk::PushConstantRange> m_push_constant_ranges;
};
}
