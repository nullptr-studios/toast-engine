#include "hardcoded_pipeline_layouts.hpp"

#include "vulkan_core.hpp"
#include "vulkan_debug.hpp"

#include <format>

namespace renderer::hardcoded_pipeline_layouts {

auto getLayoutDesc(std::string_view key) -> PipelineLayoutDesc {
	PipelineLayoutDesc desc;

	if (key == "mesh") {
		// Set 0 per-frame data, one descriptor set per frame-in-flight - binding 0 = camera UBO
		SetLayoutDesc set0;
		DescriptorBindingDesc b0;
		b0.binding = 0;
		b0.descriptor_type = vk::DescriptorType::eUniformBuffer;
		b0.descriptor_count = 1;
		b0.stage_flags = vk::ShaderStageFlagBits::eAll;    // match allocations which use eAll
		set0.bindings.push_back(b0);
		desc.sets.push_back(std::move(set0));

		// Set 1: per-material data, one descriptor set per Material - binding 0 = albedo combined image sampler
		SetLayoutDesc set1;
		DescriptorBindingDesc b1;
		b1.binding = 0;
		b1.descriptor_type = vk::DescriptorType::eCombinedImageSampler;
		b1.descriptor_count = 1;
		b1.stage_flags = vk::ShaderStageFlagBits::eFragment;
		set1.bindings.push_back(b1);
		desc.sets.push_back(std::move(set1));

		// Push constants: model matrix (64 bytes, vertex) + material color (16 bytes, fragment)
		vk::PushConstantRange pc {};
		pc.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
		pc.offset = 0;
		pc.size = 80;    // sizeof(glm::mat4) + sizeof(glm::vec4)
		desc.push_constants.push_back(pc);
		return desc;
	}

	if (key == "debug") {
		// Used by DebugPass
		SetLayoutDesc set0;
		DescriptorBindingDesc b0;
		b0.binding = 0;
		b0.descriptor_type = vk::DescriptorType::eUniformBuffer;
		b0.descriptor_count = 1;
		b0.stage_flags = vk::ShaderStageFlagBits::eAll;
		set0.bindings.push_back(b0);
		desc.sets.push_back(std::move(set0));

		// Push constants for model matrix (64 bytes)
		vk::PushConstantRange pc {};
		pc.stageFlags = vk::ShaderStageFlagBits::eVertex;
		pc.offset = 0;
		pc.size = 64;
		desc.push_constants.push_back(pc);
		return desc;
	}

	if (key == "ui_world") {
		// set 0 = camera UBO
		// set 1 = panel texture
		// push = model matrix
		SetLayoutDesc set0;
		DescriptorBindingDesc b0;
		b0.binding = 0;
		b0.descriptor_type = vk::DescriptorType::eUniformBuffer;
		b0.descriptor_count = 1;
		b0.stage_flags = vk::ShaderStageFlagBits::eAll;
		set0.bindings.push_back(b0);
		desc.sets.push_back(std::move(set0));

		SetLayoutDesc set1;
		DescriptorBindingDesc b1;
		b1.binding = 0;
		b1.descriptor_type = vk::DescriptorType::eCombinedImageSampler;
		b1.descriptor_count = 1;
		b1.stage_flags = vk::ShaderStageFlagBits::eFragment;
		set1.bindings.push_back(b1);
		desc.sets.push_back(std::move(set1));

		vk::PushConstantRange pc {};
		pc.stageFlags = vk::ShaderStageFlagBits::eVertex;
		pc.offset = 0;
		pc.size = 64;
		desc.push_constants.push_back(pc);
		return desc;
	}

	if (key == "ui_composite") {
		SetLayoutDesc set0;
		DescriptorBindingDesc b0;
		b0.binding = 0;
		b0.descriptor_type = vk::DescriptorType::eCombinedImageSampler;
		b0.descriptor_count = 1;
		b0.stage_flags = vk::ShaderStageFlagBits::eFragment;
		set0.bindings.push_back(b0);
		desc.sets.push_back(std::move(set0));
		return desc;
	}

	// Default
	return desc;
}

void buildPipelineLayout(
    const VulkanCore& core, std::string_view key, std::vector<vk::raii::DescriptorSetLayout>& out_set_layouts,
    std::vector<vk::PushConstantRange>& out_push_constants, vk::raii::PipelineLayout& out_pipeline_layout
) {
	out_set_layouts.clear();
	out_push_constants.clear();
	out_pipeline_layout = nullptr;

	const auto desc = getLayoutDesc(key);
	const auto& device = core.getDevice();

	std::vector<vk::DescriptorSetLayout> raw_handles;
	raw_handles.reserve(desc.sets.size());

	for (const auto& set : desc.sets) {
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		bindings.reserve(set.bindings.size());
		for (const auto& b : set.bindings) {
			vk::DescriptorSetLayoutBinding binding {};
			binding.binding = b.binding;
			binding.descriptorType = b.descriptor_type;
			binding.descriptorCount = b.descriptor_count;
			binding.stageFlags = b.stage_flags;
			bindings.push_back(binding);
		}
		vk::DescriptorSetLayoutCreateInfo layout_ci {};
		layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
		layout_ci.pBindings = bindings.data();

		// Create RAII object
		out_set_layouts.emplace_back(device, layout_ci);
		raw_handles.push_back(*out_set_layouts.back());
		setDebugName(core, raw_handles.back(), std::format("{} DescriptorSetLayout[{}]", key, out_set_layouts.size() - 1));
	}

	out_push_constants = desc.push_constants;

	vk::PipelineLayoutCreateInfo pipeline_layout_ci {};
	pipeline_layout_ci.setLayoutCount = static_cast<uint32_t>(raw_handles.size());
	pipeline_layout_ci.pSetLayouts = raw_handles.empty() ? nullptr : raw_handles.data();
	pipeline_layout_ci.pushConstantRangeCount = static_cast<uint32_t>(out_push_constants.size());
	pipeline_layout_ci.pPushConstantRanges = out_push_constants.empty() ? nullptr : out_push_constants.data();

	out_pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_ci);
	setDebugName(core, *out_pipeline_layout, std::format("{} PipelineLayout", key));
}

}
