/**
 * @file depth_prepass.hpp
 * @author dario
 * @date 13/08/2026
 */

#pragma once

#include "../shader_layout.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;

/// @note Only pays off while non cutout materials have no discard
class DepthPrepass {
public:
	DepthPrepass(const VulkanCore& core, vk::Format depth_format, vk::Extent2D extent);

	[[nodiscard]]
	auto isReady() const -> bool {
		return m_pipeline.isReady();
	}

	/// Expects an open rendering scope with a depth attachment and no colour attachments
	void record(vk::CommandBuffer cmd, uint32_t frame_index);

	[[nodiscard]]
	auto getDrawnCount() const noexcept -> uint32_t {
		return m_drawn;
	}

private:
	void createResources(const VulkanCore& core);

	const VulkanCore* m_core = nullptr;

	ShaderLayout m_shader_layout;
	VulkanPipeline m_pipeline;

	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;

	uint32_t m_drawn = 0;
};

}
