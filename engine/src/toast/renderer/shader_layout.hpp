#pragma once

#include "shader_reflection.hpp"

#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace renderer {
class VulkanCore;

class ShaderLayout {
public:
	ShaderLayout() = default;
	~ShaderLayout() = default;

	void rebuild(const VulkanCore& core, const ShaderReflection& reflection, std::string_view debug_name);

	[[nodiscard]]
	auto getPipelineLayout() const -> const vk::raii::PipelineLayout& {
		return m_pipeline_layout;
	}

	[[nodiscard]]
	auto getDescriptorSetLayouts() const -> const std::vector<vk::raii::DescriptorSetLayout>& {
		return m_descriptor_set_layouts;
	}

private:
	std::vector<vk::raii::DescriptorSetLayout> m_descriptor_set_layouts;
	vk::raii::PipelineLayout m_pipeline_layout = nullptr;
	std::vector<vk::PushConstantRange> m_push_constant_ranges;
};
}
