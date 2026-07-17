#include "shader_layout.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>
#include <map>
#include <toast/log.hpp>

namespace renderer {

namespace {

auto toDescriptorType(ShaderBindingKind kind) -> vk::DescriptorType {
	switch (kind) {
		case ShaderBindingKind::uniform_buffer: return vk::DescriptorType::eUniformBuffer;
		case ShaderBindingKind::storage_buffer: return vk::DescriptorType::eStorageBuffer;
		case ShaderBindingKind::combined_image_sampler: return vk::DescriptorType::eCombinedImageSampler;
		case ShaderBindingKind::sampled_image: return vk::DescriptorType::eSampledImage;
		case ShaderBindingKind::sampler: return vk::DescriptorType::eSampler;
		case ShaderBindingKind::storage_image: return vk::DescriptorType::eStorageImage;
	}
	return vk::DescriptorType::eUniformBuffer;
}

}

void ShaderLayout::rebuild(const VulkanCore& core, const ShaderReflection& reflection, std::string_view debug_name) {
	m_descriptor_set_layouts.clear();
	m_push_constant_ranges.clear();
	m_pipeline_layout = nullptr;

	const auto& device = core.getDevice();

	// Group bindings by set index
	// Sets must stay contiguous for the pipeline layout
	std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> set_map;
	for (const auto& binding : reflection.bindings) {
		vk::DescriptorSetLayoutBinding b {};
		b.binding = binding.binding;
		b.descriptorType = toDescriptorType(binding.kind);
		b.descriptorCount = binding.count == 0 ? 1u : binding.count;
		b.stageFlags = vk::ShaderStageFlagBits::eAll;
		set_map[binding.set].push_back(b);
	}

	const uint32_t set_count = set_map.empty() ? 0 : set_map.rbegin()->first + 1;
	std::vector<vk::DescriptorSetLayout> raw_handles;
	raw_handles.reserve(set_count);

	for (uint32_t set_index = 0; set_index < set_count; ++set_index) {
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		if (const auto it = set_map.find(set_index); it != set_map.end()) {
			bindings = it->second;
		}

		vk::DescriptorSetLayoutCreateInfo layout_ci {};
		layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
		layout_ci.pBindings = bindings.empty() ? nullptr : bindings.data();

		m_descriptor_set_layouts.emplace_back(device, layout_ci);
		raw_handles.push_back(*m_descriptor_set_layouts.back());
		setDebugName(core, raw_handles.back(), std::format("{} DescriptorSetLayout[{}]", debug_name, set_index));
	}

	for (const auto& push : reflection.push_constants) {
		vk::PushConstantRange range {};
		range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		range.offset = 0;
		range.size = push.size;
		if (range.size > 0) {
			m_push_constant_ranges.push_back(range);
		}
	}

	vk::PipelineLayoutCreateInfo pipeline_layout_ci {};
	pipeline_layout_ci.setLayoutCount = static_cast<uint32_t>(raw_handles.size());
	pipeline_layout_ci.pSetLayouts = raw_handles.empty() ? nullptr : raw_handles.data();
	pipeline_layout_ci.pushConstantRangeCount = static_cast<uint32_t>(m_push_constant_ranges.size());
	pipeline_layout_ci.pPushConstantRanges = m_push_constant_ranges.empty() ? nullptr : m_push_constant_ranges.data();

	m_pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_ci);
	setDebugName(core, *m_pipeline_layout, std::format("{} PipelineLayout", debug_name));
}

}
