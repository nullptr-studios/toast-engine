/// @file hiz_pyramid.hpp
/// @author dario
/// @date 14/10/2026

#pragma once

#include "gpu_sync.hpp"
#include "shader_layout.hpp"
#include "vulkan_pipeline.hpp"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;

/// Farthest depth pyramid where level k texel t covers pixels t << (k + 1) to ((t + 1) << (k + 1)) - 1
class HiZPyramid {
public:
	/// After a wait for idle since an in flight frame may still read the previous pyramid
	void create(const VulkanCore& core, vk::ImageView depth, vk::Extent2D depth_extent);

	/// Depth in depth_sampled and only its top left @p scene_extent is reduced
	void record(vk::CommandBuffer cmd, vk::Extent2D scene_extent, const glm::mat4& view_projection);

	/// Earlier writes land before a compute shader samples view()
	void beforeRead(sync::BarrierBatch& barriers);

	/// view() can be bound without it but only a ready pyramid is ever built
	[[nodiscard]]
	auto isReady() const -> bool {
		return m_view.has_value() && m_pipeline.isReady();
	}

	/// Every level in eShaderReadOnlyOptimal once recorded
	[[nodiscard]]
	auto view() const -> vk::ImageView {
		return m_view.has_value() ? **m_view : vk::ImageView {};
	}

	[[nodiscard]]
	auto sampler() const -> vk::Sampler {
		return *m_sampler;
	}

	/// What the last record() covered and saw with zero levels before the first one
	[[nodiscard]]
	auto builtExtent() const -> vk::Extent2D {
		return m_built_extent;
	}

	[[nodiscard]]
	auto builtLevels() const -> uint32_t {
		return m_built_levels;
	}

	[[nodiscard]]
	auto builtViewProjection() const -> const glm::mat4& {
		return m_built_view_projection;
	}

	/// Levels until the scene extent halves down to one texel
	[[nodiscard]]
	static auto levelCount(vk::Extent2D extent) -> uint32_t;

	/// Mirrors hiz_reduce.slang kMaxLevels so level 0 reaches 4096 texels
	static constexpr uint32_t k_max_levels = 13;

private:
	void buildPipeline(const VulkanCore& core);

	/// Mirrors hiz_reduce.slang ReducePush
	struct ReducePush {
		glm::uvec2 source_size {0};
		uint32_t level_count = 0;
		uint32_t group_count = 0;
	};

	ShaderLayout m_layout;
	VulkanPipeline m_pipeline;
	vk::raii::Sampler m_sampler = nullptr;

	std::optional<vma::raii::Image> m_image;
	std::optional<vk::raii::ImageView> m_view;
	std::vector<vk::raii::ImageView> m_level_views;
	std::optional<vk::raii::DescriptorSet> m_set;
	/// Finished groups of the reduce dispatch and 0 between dispatches
	std::optional<vma::raii::Buffer> m_counter;
	bool m_counter_cleared = false;
	sync::ImageState m_state;
	uint32_t m_levels = 0;

	vk::Extent2D m_built_extent {0, 0};
	uint32_t m_built_levels = 0;
	glm::mat4 m_built_view_projection {1.0f};
};

}
