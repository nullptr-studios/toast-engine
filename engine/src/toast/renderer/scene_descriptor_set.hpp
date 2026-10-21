/**
 * @file scene_descriptor_set.hpp
 * @author dario
 * @date 29/08/2026
 */

#pragma once

#include "shader_reflection.hpp"
#include "vulkan_common.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace renderer {
class VulkanCore;

class SceneDescriptorSets {
public:
	void create(const VulkanCore& core, const ShaderReflection& reflection, vk::DescriptorSetLayout layout, std::string_view owner);

	/// Call before the bind
	void updateTlas(uint32_t frame_index);

	[[nodiscard]]
	auto get(uint32_t frame_index) const -> vk::DescriptorSet {
		return frame_index < m_sets.size() ? *m_sets[frame_index] : vk::DescriptorSet {};
	}

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return m_sets.empty();
	}

private:
	const VulkanCore* m_core = nullptr;
	std::string m_owner;

	std::vector<vk::raii::DescriptorSet> m_sets;

	/// Reads gPunctualShadowDepths without the comparison the shadow sampler does
	vk::raii::Sampler m_shadow_depth_sampler = nullptr;

	std::optional<uint32_t> m_scene_binding;

	/// Handle plus generation since a recreated structure can reuse a destroyed handle value
	std::vector<std::pair<vk::AccelerationStructureKHR, uint64_t>> m_bound_tlas;
};

}
