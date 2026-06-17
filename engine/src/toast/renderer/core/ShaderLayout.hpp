#pragma once

#include <cstdint>
#include <map>
#include <slang.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace toast::renderer {
class VulkanCore;

class ShaderLayout {
public:
	ShaderLayout() = default;
	ShaderLayout(const VulkanCore& core, slang::ProgramLayout* slang_layout);
	~ShaderLayout() = default;

	void rebuild(const VulkanCore& core, slang::ProgramLayout* slang_layout);

	[[nodiscard]]
	auto getPipelineLayout() const -> const vk::raii::PipelineLayout& {
		return m_pipeline_layout;
	}

	[[nodiscard]]
	auto getDescriptorSetLayouts() const -> const std::vector<vk::raii::DescriptorSetLayout>& {
		return m_descriptor_set_layouts;
	}

private:
	void reflectBindings(
	    slang::VariableLayoutReflection* var_layout, uint32_t current_space, uint32_t current_binding,
	    std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>>& set_layout_map
	);

	auto mapResourceToDescriptorType(slang::TypeLayoutReflection* type_layout) const -> vk::DescriptorType;

	std::vector<vk::raii::DescriptorSetLayout> m_descriptor_set_layouts;
	vk::raii::PipelineLayout m_pipeline_layout = nullptr;
	std::vector<vk::PushConstantRange> m_push_constant_ranges;
};
}
