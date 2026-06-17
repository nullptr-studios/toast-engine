#include "ShaderLayout.hpp"

#include "VulkanCore.hpp"

namespace toast::renderer {
ShaderLayout::ShaderLayout(const VulkanCore& core, slang::ProgramLayout* slang_layout) {
	rebuild(core, slang_layout);
}

void ShaderLayout::rebuild(const VulkanCore& core, slang::ProgramLayout* slang_layout) {
	m_descriptor_set_layouts.clear();
	m_push_constant_ranges.clear();
	m_pipeline_layout = nullptr;

	if (!slang_layout) {
		return;
	}

	// Map to group generated bindings by their accumulated SubElementRegisterSpace
	std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> set_layout_map;

	// Traverse Global Scope Parameters
	if (auto* global_params = slang_layout->getGlobalParamsVarLayout()) {
		reflectBindings(global_params, 0, 0, set_layout_map);
	}

	// 2. Traverse Entry Points (Vertex, Fragment, Compute, etc.)
	uint32_t entry_point_count = slang_layout->getEntryPointCount();
	for (uint32_t i = 0; i < entry_point_count; ++i) {
		if (auto* entry_point = slang_layout->getEntryPointByIndex(i)) {
			if (auto* var_layout = entry_point->getVarLayout()) {
				reflectBindings(var_layout, 0, 0, set_layout_map);
			}
		}
	}

	// 3. Construct the Descriptor Set Layouts
	uint32_t max_set_index = 0;
	for (const auto& [set_index, bindings] : set_layout_map) {
		if (set_index > max_set_index) {
			max_set_index = set_index;
		}
	}

	m_descriptor_set_layouts.reserve(max_set_index + 1);
	std::vector<vk::DescriptorSetLayout> raw_layout_handles;
	raw_layout_handles.reserve(max_set_index + 1);

	for (uint32_t i = 0; i <= max_set_index; ++i) {
		vk::DescriptorSetLayoutCreateInfo layout_ci {};

		auto it = set_layout_map.find(i);
		if (it != set_layout_map.end()) {
			layout_ci.bindingCount = static_cast<uint32_t>(it->second.size());
			layout_ci.pBindings = it->second.data();
		} else {
			layout_ci.bindingCount = 0;
			layout_ci.pBindings = nullptr;
		}

		// Instantiating modern Vulkan RAII descriptor set layout
		m_descriptor_set_layouts.emplace_back(core.getDevice(), layout_ci);
		raw_layout_handles.push_back(*m_descriptor_set_layouts.back());
	}

	// Construct Pipeline Layout
	vk::PipelineLayoutCreateInfo pipeline_layout_ci {};
	pipeline_layout_ci.setLayoutCount = static_cast<uint32_t>(raw_layout_handles.size());
	pipeline_layout_ci.pSetLayouts = raw_layout_handles.data();
	pipeline_layout_ci.pushConstantRangeCount = static_cast<uint32_t>(m_push_constant_ranges.size());
	pipeline_layout_ci.pPushConstantRanges = m_push_constant_ranges.data();

	m_pipeline_layout = vk::raii::PipelineLayout(core.getDevice(), pipeline_layout_ci);
}

void ShaderLayout::reflectBindings(
    slang::VariableLayoutReflection* var_layout, uint32_t current_space, uint32_t current_binding,
    std::map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>>& set_layout_map
) {
	if (!var_layout) {
		return;
	}

	uint32_t binding_offset = var_layout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
	uint32_t space_offset = var_layout->getOffset(slang::ParameterCategory::SubElementRegisterSpace);

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
		// Track every stage this pipeline block touches (e.g., Vertex, Fragment, or eAll)
		range.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

		// Pull the offset using the explicit PushConstantBuffer category
		range.offset = static_cast<uint32_t>(var_layout->getOffset(slang::ParameterCategory::PushConstantBuffer));

		// Safe Size Check: Determine size based on wrapper type layout
		if (type_layout->getKind() == slang::TypeReflection::Kind::ConstantBuffer) {
			// If wrapped in ConstantBuffer<T>, query the element type inside it
			auto element_layout = type_layout->getElementTypeLayout();
			range.size = static_cast<uint32_t>(element_layout->getSize(slang::ParameterCategory::Uniform));
		} else {
			// If declared as a raw struct block
			range.size = static_cast<uint32_t>(type_layout->getSize(slang::ParameterCategory::Uniform));
		}

		if (range.size > 0) {
			m_push_constant_ranges.push_back(range);
		}

		// Return early so this parameter isn't mistakenly added to your descriptor set layouts!
		return;
	}

	// Handle Container Splitting: ConstantBuffer<T> vs ParameterBlock<T>
	if (type_kind == slang::TypeReflection::Kind::ConstantBuffer || type_kind == slang::TypeReflection::Kind::ParameterBlock) {
		slang::VariableLayoutReflection* container_layout = type_layout->getContainerVarLayout();
		slang::VariableLayoutReflection* element_layout = type_layout->getElementVarLayout();

		if (type_kind == slang::TypeReflection::Kind::ConstantBuffer) {
			// A ConstantBuffer acts as a unique Uniform Buffer descriptor binding in Vulkan.
			vk::DescriptorSetLayoutBinding binding {};
			binding.binding = next_binding;
			binding.descriptorType = vk::DescriptorType::eUniformBuffer;
			binding.descriptorCount = 1;
			binding.stageFlags = vk::ShaderStageFlagBits::eAll;
			set_layout_map[next_space].push_back(binding);

			// Per structural constraints: stop accumulating byte uniform offsets down this branch.
			if (element_layout) {
				reflectBindings(element_layout, next_space, next_binding, set_layout_map);
			}
		} else if (type_kind == slang::TypeReflection::Kind::ParameterBlock) {
			// A ParameterBlock shifts elements into a completely isolated Descriptor Space/Set.
			// Stop accumulating binding offsets here; nested sub-elements reset relative to 0 inside the new space.
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
		uint32_t element_count = static_cast<uint32_t>(type_layout->getElementCount());
		slang::TypeLayoutReflection* element_type_layout = type_layout->getElementTypeLayout();

		if (element_type_layout) {
			slang::TypeReflection::Kind element_kind = element_type_layout->getKind();

			// Filter for valid Vulkan descriptor types
			if (element_kind == slang::TypeReflection::Kind::Resource || element_kind == slang::TypeReflection::Kind::SamplerState) {
				vk::DescriptorSetLayoutBinding binding {};
				binding.binding = next_binding;
				binding.descriptorType = mapResourceToDescriptorType(element_type_layout);
				// element_count == 0 means an unbounded/bindless descriptor array runtime size
				binding.descriptorCount = (element_count == 0) ? 1 : element_count;
				binding.stageFlags = vk::ShaderStageFlagBits::eAll;
				set_layout_map[next_space].push_back(binding);
			}
		}
		return;
	}

	// Handle Standalone Leaf Resource Bindings
	if (var_layout->getCategory() == slang::ParameterCategory::DescriptorTableSlot) {
		vk::DescriptorSetLayoutBinding binding {};
		binding.binding = next_binding;
		binding.descriptorType = mapResourceToDescriptorType(type_layout);
		binding.descriptorCount = 1;
		binding.stageFlags = vk::ShaderStageFlagBits::eAll;
		set_layout_map[next_space].push_back(binding);
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
