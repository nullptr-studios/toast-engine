/// @file ShaderLayout.cpp
/// @author dario
/// @date 07/06/2026.

#include "ShaderLayout.hpp"

#include "VulkanCore.hpp"
#include "toast/log.hpp"

namespace toast::renderer {

ShaderLayout::ShaderLayout(const VulkanCore& core, slang::ProgramLayout* slang_layout) {
	rebuild(core, slang_layout);
}

void ShaderLayout::rebuild(const VulkanCore& core, slang::ProgramLayout* slang_layout) {
	m_pipeline_layout = nullptr;
	m_descriptor_set_layouts.clear();
	m_push_constant_ranges.clear();

	const auto& device = core.getDevice();

	std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> set_layout_map;

	const uint32_t parameter_count = slang_layout->getParameterCount();

	for (uint32_t i = 0; i < parameter_count; ++i) {
		auto* var_layout = slang_layout->getParameterByIndex(i);

		auto* type_layout = var_layout->getTypeLayout();

		bool is_push_constant = false;

		for (unsigned c = 0; c < var_layout->getCategoryCount(); ++c) {
			auto category = var_layout->getCategoryByIndex(c);

			if (category == slang::ParameterCategory::PushConstantBuffer) {
				vk::PushConstantRange range {};

				range.stageFlags = vk::ShaderStageFlagBits::eAll;

				range.offset = 0;

				range.size = static_cast<uint32_t>(type_layout->getSize());

				TOAST_INFO("ShaderLayout", "Push constant '{}' offset={} size={}", var_layout->getName(), range.offset, range.size);

				if (range.size == 0) {
					TOAST_CRITICAL("ShaderLayout", "Push constant '{}' has size 0", var_layout->getName());
				}

				m_push_constant_ranges.push_back(range);

				is_push_constant = true;
				break;
			}
		}

		if (is_push_constant) {
			continue;
		}

		reflectBindings(var_layout, 0, 0, set_layout_map);
	}

	uint32_t max_set_index = set_layout_map.empty() ? 0 : set_layout_map.rbegin()->first;

	m_descriptor_set_layouts.reserve(max_set_index + 1);

	for (uint32_t i = 0; i <= max_set_index; ++i) {
		if (set_layout_map.contains(i)) {
			const auto& bindings = set_layout_map[i];

			vk::DescriptorSetLayoutCreateInfo layout_ci({}, static_cast<uint32_t>(bindings.size()), bindings.data());

			m_descriptor_set_layouts.emplace_back(device, layout_ci);
		} else {
			vk::DescriptorSetLayoutCreateInfo layout_ci({}, 0, nullptr);

			m_descriptor_set_layouts.emplace_back(device, layout_ci);
		}
	}

	std::vector<vk::DescriptorSetLayout> raw_set_layouts;

	raw_set_layouts.reserve(m_descriptor_set_layouts.size());

	for (const auto& layout : m_descriptor_set_layouts) {
		raw_set_layouts.push_back(*layout);
	}

	vk::PipelineLayoutCreateInfo pipeline_layout_ci(
	    {},
	    static_cast<uint32_t>(raw_set_layouts.size()),
	    raw_set_layouts.data(),
	    static_cast<uint32_t>(m_push_constant_ranges.size()),
	    m_push_constant_ranges.data()
	);

	m_pipeline_layout = vk::raii::PipelineLayout(device, pipeline_layout_ci);
}

void ShaderLayout::reflectBindings(
    slang::VariableLayoutReflection* var_layout, uint32_t current_space, uint32_t current_binding,
    std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>>& set_layout_map
) {
	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();

	// Accumulate relative offsets into absolute bindings based on the layout unit
	uint32_t space = current_space;
	uint32_t binding = current_binding;

	for (unsigned c = 0; c < var_layout->getCategoryCount(); ++c) {
		auto cat = var_layout->getCategoryByIndex(c);
		if (cat == slang::ParameterCategory::DescriptorTableSlot) {
			space += static_cast<uint32_t>(var_layout->getBindingSpace(cat));
			binding += static_cast<uint32_t>(var_layout->getOffset(cat));
		} else if (cat == slang::ParameterCategory::SubElementRegisterSpace) {
			space += static_cast<uint32_t>(var_layout->getOffset(cat));
		}
	}

	auto kind = type_layout->getKind();

	// A. Struct Traversal
	if (kind == slang::TypeReflection::Kind::Struct) {
		int field_count = type_layout->getFieldCount();
		for (int f = 0; f < field_count; ++f) {
			reflectBindings(type_layout->getFieldByIndex(f), space, binding, set_layout_map);
		}
	}
	// B. Containers (ParameterBlock, ConstantBuffer)
	else if (kind == slang::TypeReflection::Kind::ParameterBlock || kind == slang::TypeReflection::Kind::ConstantBuffer) {
		// 1. Process the Container (Checks for implicitly allocated constant buffers)
		slang::VariableLayoutReflection* container_layout = type_layout->getContainerVarLayout();
		for (unsigned c = 0; c < container_layout->getCategoryCount(); ++c) {
			auto cat = container_layout->getCategoryByIndex(c);
			if (cat == slang::ParameterCategory::DescriptorTableSlot) {
				vk::DescriptorSetLayoutBinding b {};
				b.binding = binding + static_cast<uint32_t>(container_layout->getOffset(cat));
				b.descriptorType = vk::DescriptorType::eUniformBuffer;
				b.descriptorCount = 1;
				b.stageFlags = vk::ShaderStageFlagBits::eAll;

				uint32_t c_space = space + static_cast<uint32_t>(container_layout->getBindingSpace(cat));
				set_layout_map[c_space].push_back(b);
			}
		}

		// 2. Process the Element
		reflectBindings(type_layout->getElementVarLayout(), space, binding, set_layout_map);
	}
	// C. Base Resources
	else if (kind == slang::TypeReflection::Kind::Resource || kind == slang::TypeReflection::Kind::SamplerState) {
		vk::DescriptorSetLayoutBinding b {};
		b.binding = binding;
		b.descriptorType = mapResourceToDescriptorType(type_layout);
		b.descriptorCount = 1;
		b.stageFlags = vk::ShaderStageFlagBits::eAll;

		set_layout_map[space].push_back(b);
	}
	// D. Arrays
	else if (kind == slang::TypeReflection::Kind::Array) {
		slang::TypeLayoutReflection* element_layout = type_layout->getElementTypeLayout();

		if (element_layout->getKind() == slang::TypeReflection::Kind::Resource ||
		    element_layout->getKind() == slang::TypeReflection::Kind::SamplerState) {
			vk::DescriptorSetLayoutBinding b {};
			b.binding = binding;
			b.descriptorType = mapResourceToDescriptorType(element_layout);

			size_t element_count = type_layout->getElementCount();
			b.descriptorCount = (element_count == ~size_t(0)) ? 1024 : static_cast<uint32_t>(element_count);    // Handle unbounded
			b.stageFlags = vk::ShaderStageFlagBits::eAll;

			set_layout_map[space].push_back(b);
		}
		// (Arrays of structs containing resources would be added here)
	}
}

auto ShaderLayout::mapResourceToDescriptorType(slang::TypeLayoutReflection* type_layout) const -> vk::DescriptorType {
	if (type_layout->getKind() == slang::TypeReflection::Kind::SamplerState) {
		return vk::DescriptorType::eSampler;
	}

	SlangResourceShape shape = type_layout->getResourceShape();
	SlangResourceAccess access = type_layout->getResourceAccess();

	// Map based on access modifiers as explained in "Resources" docs
	if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
		// e.g., RWTexture2D
		return vk::DescriptorType::eStorageImage;
	}

	// Default to Sampled Image (e.g., Texture2D without explicit RW prefix)
	return vk::DescriptorType::eSampledImage;
}

}    // namespace toast::renderer
