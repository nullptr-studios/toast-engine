/**
 * @file traced_specular_pass.hpp
 * @author dario
 * @date 25/10/2026
 */

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/hiz_pyramid.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/resources/scene_descriptor_set.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

/// GGX rays for reflective voxels at half resolution in place of their environment specular
class TracedSpecularPass : public IPostProcessPass {
public:
	TracedSpecularPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Traced specular";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

	[[nodiscard]]
	auto isReady() const noexcept -> bool {
		return m_ready;
	}

private:
	/// Mirrors voxel_specular.slang SpecularParams
	struct Params {
		glm::mat4 inverse_view_projection {1.0f};
		/// x reach in meters y max roughness z 1 with a valid history w sun reach in meters
		glm::vec4 tracing {0.0f};
		/// x 1 when binding 10 holds last frame resolve
		glm::vec4 resolve {0.0f};
		/// x levels of the closest depth pyramid y most screen march steps with 0 off z SSR thickness w 1 when off screen
		/// hits take local lights
		glm::vec4 march {0.0f};
	};

	/// rgb radiance a hit distance then history and -1 where nothing was traced
	static constexpr vk::Format k_trace_format = vk::Format::eR16G16B16A16Sfloat;
	/// voxel_specular.slang packSurface
	static constexpr vk::Format k_surface_format = vk::Format::eR32G32B32A32Uint;

	/// Last frame resolve box filtered to half resolution then halved per level so rough screen hits read it blurred
	static constexpr vk::Format k_pyramid_format = vk::Format::eR16G16B16A16Sfloat;
	/// Mirrors voxel_specular.slang kPyramidLevels
	static constexpr uint32_t k_pyramid_levels = 6;

	static constexpr uint32_t k_image_bindings = 16;

	void createSets(const VulkanCore& core);
	void createTargets(const VulkanCore& core, vk::Extent2D extent);
	void createPyramid(const VulkanCore& core, vk::Extent2D extent);
	/// Level 0 drawn from binding 10 then each level blitted from the one above
	void recordPyramid(
	    vk::CommandBuffer cmd, vk::PipelineLayout layout, std::span<const vk::DescriptorSet> sets, const Params& params
	);

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;
	bool m_ready = false;

	ShaderLayout m_layout;
	VulkanPipeline m_surface_pipeline;
	VulkanPipeline m_trace_pipeline;
	VulkanPipeline m_resolve_pipeline;
	VulkanPipeline m_temporal_pipeline;
	VulkanPipeline m_composite_pipeline;
	VulkanPipeline m_pyramid_pipeline;
	VulkanPipeline m_filter_pipeline;
	SceneDescriptorSets m_scene_sets;
	bool m_instances_bound = false;

	vk::raii::Sampler m_point_sampler = nullptr;
	/// Trilinear across the pyramid levels
	vk::raii::Sampler m_pyramid_sampler = nullptr;

	/// Per frame in flight
	std::vector<vk::raii::DescriptorSet> m_storage_sets;
	DescriptorCache<std::shared_ptr<const VoxelGpuStorage>> m_storage_bound;
	/// Per frame in flight then per history parity
	std::vector<vk::raii::DescriptorSet> m_image_sets;
	DescriptorCache<std::array<vk::ImageView, k_image_bindings>> m_image_bound;

	PostProcessTarget m_surface;
	PostProcessTarget m_trace;
	PostProcessTarget m_resolved;
	/// The temporal pass reads one and writes the other
	std::array<PostProcessTarget, 2> m_history;
	/// Footprint filtered history at half resolution so the full resolution composite only upsamples it
	PostProcessTarget m_filtered;
	PostProcessTarget m_target;
	/// Closest depth so a ray the screen shows skips the voxel trace
	HiZPyramid m_hiz;

	std::optional<vma::raii::Image> m_pyramid;
	/// Every level for sampling and level 0 alone for drawing
	std::optional<vk::raii::ImageView> m_pyramid_view;
	std::optional<vk::raii::ImageView> m_pyramid_top_view;
	vk::Extent2D m_pyramid_extent {};
	uint32_t m_pyramid_level_count = 0;
	sync::ImageState m_pyramid_state;

	uint32_t m_parity = 0;
	bool m_history_valid = false;
	/// A gap in frames or a recreated target restarts the history
	uint64_t m_last_sequence = 0;
};

}
