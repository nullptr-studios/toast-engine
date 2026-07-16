#include "shader_layout.hpp"

#include "hardcoded_pipeline_layouts.hpp"
#include "vulkan_core.hpp"

#include <algorithm>
#include <utility>

namespace renderer {
ShaderLayout::ShaderLayout(const VulkanCore& core, slang::ProgramLayout* slang_layout) {
	rebuild(core, "default");
}

// Hardcoded-only rebuild: ignore Slang reflection and build from the central table
void ShaderLayout::rebuild(const VulkanCore& core, std::string_view shader_key) {
	m_descriptor_set_layouts.clear();
	m_push_constant_ranges.clear();
	m_pipeline_layout = nullptr;

	hardcoded_pipeline_layouts::buildPipelineLayout(
	    core, shader_key, m_descriptor_set_layouts, m_push_constant_ranges, m_pipeline_layout
	);
}

// Compatibility: keep old signature but redirect to hardcoded path
void ShaderLayout::rebuild(const VulkanCore& core, slang::ProgramLayout* /*slang_layout*/) {
	// Use a generic default key when callers pass a Slang layout
	rebuild(core, "default");
}

void ShaderLayout::reflectBindings(
    slang::VariableLayoutReflection* var_layout, uint32_t current_space, uint32_t current_binding,
    std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>>& set_layout_map
) {
	if (!var_layout) {
		return;
	}

	uint32_t binding_offset = static_cast<uint32_t>(var_layout->getOffset(slang::ParameterCategory::DescriptorTableSlot));
	uint32_t space_offset = static_cast<uint32_t>(var_layout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot));

	uint32_t next_space = current_space + space_offset;
	uint32_t next_binding = current_binding + binding_offset;

	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
	if (!type_layout) {
		return;
	}

	slang::TypeReflection::Kind type_kind = type_layout->getKind();

	// Check all categories since a Slang parameter layout can belong to multiple categories
	bool is_push_constant = false;
	uint32_t category_count = var_layout->getCategoryCount();

	for (uint32_t c = 0; c < category_count; ++c) {
		if (var_layout->getCategoryByIndex(c) == slang::ParameterCategory::PushConstantBuffer) {
			is_push_constant = true;
			break;
		}
	}

	if (is_push_constant) {
		vk::PushConstantRange range {};
		// Track every stage this pipeline block touches
		range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

		// Pull the offset using the explicit PushConstantBuffer category
		range.offset = static_cast<uint32_t>(var_layout->getOffset(slang::ParameterCategory::PushConstantBuffer));

		if (type_layout->getKind() == slang::TypeReflection::Kind::ConstantBuffer) {
			auto* element_layout = type_layout->getElementTypeLayout();
			range.size = static_cast<uint32_t>(element_layout->getSize(slang::ParameterCategory::Uniform));
		} else {
			// If declared as a raw struct block
			range.size = static_cast<uint32_t>(type_layout->getSize(slang::ParameterCategory::Uniform));
		}

		if (range.size > 0) {
			m_push_constant_ranges.push_back(range);
		}

		return;
	}

	if (type_kind == slang::TypeReflection::Kind::ConstantBuffer || type_kind == slang::TypeReflection::Kind::ParameterBlock) {
		slang::VariableLayoutReflection* container_layout = type_layout->getContainerVarLayout();
		slang::VariableLayoutReflection* element_layout = type_layout->getElementVarLayout();

		if (type_kind == slang::TypeReflection::Kind::ConstantBuffer) {
			vk::DescriptorSetLayoutBinding binding {};
			binding.binding = next_binding;
			binding.descriptorType = vk::DescriptorType::eUniformBuffer;
			binding.descriptorCount = 1;
			binding.stageFlags = vk::ShaderStageFlagBits::eAll;
			set_layout_map[next_space].push_back(binding);

			if (element_layout) {
				reflectBindings(element_layout, next_space, next_binding, set_layout_map);
			}
		} else if (type_kind == slang::TypeReflection::Kind::ParameterBlock) {
			if (element_layout) {
				reflectBindings(element_layout, next_space, 0, set_layout_map);
			}
		}
		return;
	}

	// Handle Structs Traversal
	if (type_kind == slang::TypeReflection::Kind::Struct) {
		uint32_t field_count = type_layout->getFieldCount();
		for (uint32_t i = 0; i < field_count; ++i) {
			reflectBindings(type_layout->getFieldByIndex(i), next_space, next_binding, set_layout_map);
		}
		return;
	}

	if (type_kind == slang::TypeReflection::Kind::Array) {
		size_t element_count = type_layout->getElementCount();
		slang::TypeLayoutReflection* element_type_layout = type_layout->getElementTypeLayout();

		if (element_type_layout) {
			slang::TypeReflection::Kind element_kind = element_type_layout->getKind();

			// Filter for valid Vulkan descriptor types
			if (element_kind == slang::TypeReflection::Kind::Resource || element_kind == slang::TypeReflection::Kind::SamplerState) {
				vk::DescriptorSetLayoutBinding binding {};
				binding.binding = next_binding;
				binding.descriptorType = mapResourceToDescriptorType(element_type_layout);
				// Unbounded arrays return ~size_t(0) from Slang; treat them as runtime-sized bindless descriptors (use descriptorCount=1
				// here)
				binding.descriptorCount = (std::cmp_equal(element_count, ~0)) ? 1u : static_cast<uint32_t>(element_count);
				binding.stageFlags = vk::ShaderStageFlagBits::eAll;
				set_layout_map[next_space].push_back(binding);
			}
		}
		return;
	}

	// Handle Standalone Leaf Resource Bindings (check if this variable is in a descriptor table/slot)
	{
		bool in_descriptor_table = false;
		uint32_t cat_count = var_layout->getCategoryCount();
		for (uint32_t ci = 0; ci < cat_count; ++ci) {
			if (var_layout->getCategoryByIndex(ci) == slang::ParameterCategory::DescriptorTableSlot) {
				in_descriptor_table = true;
				break;
			}
		}
		if (in_descriptor_table) {
			vk::DescriptorSetLayoutBinding binding {};
			binding.binding = next_binding;
			binding.descriptorType = mapResourceToDescriptorType(type_layout);
			binding.descriptorCount = 1;
			binding.stageFlags = vk::ShaderStageFlagBits::eAll;
			set_layout_map[next_space].push_back(binding);
		}
	}
}

auto ShaderLayout::mapResourceToDescriptorType(slang::TypeLayoutReflection* type_layout) const -> vk::DescriptorType {
	if (!type_layout) {
		return vk::DescriptorType::eStorageBuffer;
	}

	slang::TypeReflection::Kind kind = type_layout->getKind();

	// Separate Samplers (SamplerState / SamplerComparisonState)
	if (kind == slang::TypeReflection::Kind::SamplerState) {
		return vk::DescriptorType::eSampler;
	}

	// Explicit Constant Buffers
	if (kind == slang::TypeReflection::Kind::ConstantBuffer) {
		return vk::DescriptorType::eUniformBuffer;
	}

	// Explicit Storage Buffers
	if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer) {
		return vk::DescriptorType::eStorageBuffer;
	}

	// Resources (Textures, StructuredBuffers, ByteAddressBuffers)
	if (kind == slang::TypeReflection::Kind::Resource) {
		SlangResourceShape shape = type_layout->getResourceShape();
		SlangResourceAccess access = type_layout->getResourceAccess();

		switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK) {
			case SLANG_TEXTURE_1D:
			case SLANG_TEXTURE_2D:
			case SLANG_TEXTURE_3D:
			case SLANG_TEXTURE_CUBE:
				if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
					return vk::DescriptorType::eStorageImage;    // RWTexture2D -> imageStore/imageLoad
				} else {
					return vk::DescriptorType::eSampledImage;    // Texture2D -> Texture paired with separate sampler
				}
			case SLANG_STRUCTURED_BUFFER:
			case SLANG_BYTE_ADDRESS_BUFFER: return vk::DescriptorType::eStorageBuffer;
			default: break;
		}
	}

	return vk::DescriptorType::eUniformBuffer;
}
}
