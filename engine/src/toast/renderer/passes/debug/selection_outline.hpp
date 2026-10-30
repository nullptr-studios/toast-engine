/**
 * @file selection_outline.hpp
 * @author dario
 * @date 16/10/2026
 */

#pragma once

#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

/// Outlines the nodes selected in the editor hierarchy through a stencil mask
class SelectionOutline {
public:
	SelectionOutline(const VulkanCore& core, vk::Format output_format, vk::Format depth_format, vk::Extent2D extent);

	/// After a device wait idle
	void onResize(vk::Extent2D extent);

	/// Outside any rendering scope with the scene depth in sync::Usage::depth_sampled
	void recordMask(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView scene_depth);

	/// Inside the output scope after recordMask
	void recordOutline(vk::CommandBuffer cmd, uint32_t frame_index);

private:
	/// Mirrors selection_mask.slang PushConstants
	struct MaskPush {
		uint32_t instance = 0;
		uint32_t test_occlusion = 0;
	};

	/// Mirrors selection_outline.slang PushConstants
	struct OutlinePush {
		glm::vec4 solid_color {0.0f};
		glm::vec4 hidden_color {0.0f};
		glm::ivec4 rect {0};
		float blend = 1.0f;
		std::array<float, 3> pad {};
	};

	static constexpr uint32_t k_max_volumes = 64;

	void createTargets(vk::Extent2D extent);
	void createMaskPipelines(vk::Extent2D extent);
	void createOutlinePipelines(vk::Format output_format, vk::Format depth_format, vk::Extent2D extent);

	/// Bound voxel instances or 0 when no outlined volume can draw
	auto prepareVolumes(uint32_t frame_index) -> uint32_t;

	/// Averages the outline over the jitter sequence while the camera and selection hold still
	void recordResolve(vk::CommandBuffer cmd, uint32_t frame_index, bool reset);

	const VulkanCore* m_core = nullptr;

	vk::Format m_mask_format = vk::Format::eUndefined;
	vk::Extent2D m_extent {};
	std::optional<vma::raii::Image> m_mask;
	/// Every aspect for the attachment and the stencil alone for sampling
	std::optional<vk::raii::ImageView> m_mask_attachment;
	std::optional<vk::raii::ImageView> m_mask_sampled;
	sync::ImageState m_mask_state;
	std::optional<vk::raii::Sampler> m_sampler;

	ShaderLayout m_mask_layout;
	VulkanPipeline m_mesh_pipeline;
	VulkanPipeline m_voxel_pipeline;
	std::vector<vk::raii::DescriptorSet> m_mask_sets;
	std::vector<vk::raii::DescriptorSet> m_voxel_sets;
	std::vector<vma::raii::Buffer> m_volume_buffers;
	std::vector<vk::ImageView> m_bound_depth;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_bound_storage;

	ShaderLayout m_outline_layout;
	VulkanPipeline m_resolve_pipeline;
	VulkanPipeline m_composite_pipeline;
	std::vector<vk::raii::DescriptorSet> m_outline_sets;
	/// Mask history and outline views each set was written with
	std::vector<std::array<vk::ImageView, 3>> m_bound_outline;

	/// Ping pong where m_write is the one this frame resolves into
	std::array<PostProcessTarget, 2> m_coverage;
	uint32_t m_write = 0;
	bool m_history_valid = false;
	glm::mat4 m_history_view_projection {1.0f};
	std::vector<glm::mat4> m_history_models;

	/// The output scope composites this frame
	bool m_drawn = false;
	vk::Rect2D m_outline_rect {};
};

}
