#pragma once

#include <string_view>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace toast::renderer {
class VulkanCore;

namespace HardcodedPipelineLayouts {

struct DescriptorBindingDesc {
	uint32_t binding = 0;
	vk::DescriptorType descriptor_type = vk::DescriptorType::eUniformBuffer;
	uint32_t descriptor_count = 1;
	vk::ShaderStageFlags stage_flags = vk::ShaderStageFlagBits::eAll;
};

struct SetLayoutDesc {
	std::vector<DescriptorBindingDesc> bindings;
};

struct PipelineLayoutDesc {
	std::vector<SetLayoutDesc> sets;
	std::vector<vk::PushConstantRange> push_constants;
};

// Return a small descriptor/pipeline description for a known shader key
PipelineLayoutDesc getLayoutDesc(std::string_view key);

// Build actual Vulkan RAII objects from the description
void buildPipelineLayout(
    const VulkanCore& core, std::string_view key, std::vector<vk::raii::DescriptorSetLayout>& out_set_layouts,
    std::vector<vk::PushConstantRange>& out_push_constants, vk::raii::PipelineLayout& out_pipeline_layout
);

}    // namespace HardcodedPipelineLayouts
}    // namespace toast::renderer
