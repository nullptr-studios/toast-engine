#include "hardcoded_pipeline_layouts.hpp"

#include "vulkan_core.hpp"

namespace toast::renderer {
namespace HardcodedPipelineLayouts {

PipelineLayoutDesc getLayoutDesc(std::string_view key) {
	PipelineLayoutDesc desc;

	if (key == "mesh") {
		// Common mesh shader: set 0 binding 0 = camera UBO (uniform buffer)
		// and binding 1 = combined image sampler for albedo
		SetLayoutDesc set0;
		DescriptorBindingDesc b0;
		b0.binding = 0;
		b0.descriptor_type = vk::DescriptorType::eUniformBuffer;
		b0.descriptor_count = 1;
		b0.stage_flags = vk::ShaderStageFlagBits::eAll;    // match allocations which use eAll
		set0.bindings.push_back(b0);

		DescriptorBindingDesc b1;
		b1.binding = 1;
		b1.descriptor_type = vk::DescriptorType::eCombinedImageSampler;
		b1.descriptor_count = 1;
		b1.stage_flags = vk::ShaderStageFlagBits::eFragment;
		set0.bindings.push_back(b1);

		desc.sets.push_back(std::move(set0));

		// Push constants for model matrix (64 bytes)
		vk::PushConstantRange pc {};
		pc.stageFlags = vk::ShaderStageFlagBits::eVertex;
		pc.offset = 0;
		pc.size = 64;    // sizeof(glm::mat4)
		desc.push_constants.push_back(pc);
		return desc;
	}

	// Default: empty layout (no sets, no push constants)
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
	auto& device = core.getDevice();

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

		// Create RAII object (it copies the layout info immediately)
		out_set_layouts.emplace_back(device, layout_ci);
		raw_handles.push_back(*out_set_layouts.back());
	}

	out_push_constants = desc.push_constants;

	vk::PipelineLayoutCreateInfo pipeline_layout_ci {};
	pipeline_layout_ci.setLayoutCount = static_cast<uint32_t>(raw_handles.size());
	pipeline_layout_ci.pSetLayouts = raw_handles.empty() ? nullptr : raw_handles.data();
	pipeline_layout_ci.pushConstantRangeCount = static_cast<uint32_t>(out_push_constants.size());
	pipeline_layout_ci.pPushConstantRanges = out_push_constants.empty() ? nullptr : out_push_constants.data();

	out_pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_ci);
}

}    // namespace HardcodedPipelineLayouts
}    // namespace toast::renderer
