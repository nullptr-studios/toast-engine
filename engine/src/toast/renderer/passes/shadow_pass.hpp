/**
 * @file shadow_pass.hpp
 * @author dario
 * @date 01/08/2026
 */

#pragma once

#include "../render_pass_base.hpp"
#include "../shader_layout.hpp"
#include "../shadow_constants.hpp"
#include "../voxel_change_history.hpp"
#include "../vulkan_common.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

[[nodiscard]]
auto createShadowSampler(const VulkanCore& core, vk::Format format) -> vk::raii::Sampler;

class ShadowPass : public IRenderPass {
public:
	explicit ShadowPass(const VulkanCore& core);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "ShadowPass";
	}

	void recordPre(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	/// Everything records in recordPre()
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override { }

	[[nodiscard]]
	auto getCascadeMapView(uint32_t frame_index) const -> vk::ImageView;

	[[nodiscard]]
	auto getPunctualMapView(uint32_t frame_index) const -> vk::ImageView;

	[[nodiscard]]
	auto getShadowSampler() const -> vk::Sampler {
		return *m_sampler;
	}

	/// Per frame and per layer group unless noted
	struct Stats {
		uint32_t draws = 0;
		uint32_t scopes = 0;
		/// Slot groups left as they were
		uint32_t cached = 0;
		/// Slot groups rebuilt from their static copy and the moving casters
		uint32_t composed = 0;
		/// Static renders whose views moved
		uint32_t static_views = 0;
		/// Static renders whose resting voxel casters changed
		uint32_t static_casters = 0;
		/// Static renders for a voxel edit reaching them
		uint32_t static_edits = 0;
		uint32_t moving_volumes = 0;
		/// Cached static groups that a voxel edit elsewhere would have re-rendered
		uint32_t spared = 0;
		/// Voxel volume draws rasterized from task and mesh shader quads
		uint32_t meshed_volumes = 0;
		/// Voxel volume draws marched per texel
		uint32_t marched_volumes = 0;
	};

	[[nodiscard]]
	auto getStats() const noexcept -> const Stats& {
		return m_stats;
	}

private:
	/// Mirrors voxel_shadow.slang ShadowUBO and shadow_depth.slang reads only the first array
	struct ShadowUBO {
		std::array<glm::mat4, shadows::k_max_shadow_views> view_projection {};
		std::array<glm::mat4, shadows::k_max_shadow_views> inverse_view_projection {};
	};

	/// Mirrors shadow_depth.slang PushConstants
	struct ShadowPushConstants {
		uint32_t instance_base = 0;
		uint32_t view_index = 0;
	};

	/// Mirrors voxel_shadow.slang PushConstants
	struct VoxelShadowPush {
		uint32_t instance_index = 0;
		uint32_t view_index = 0;
		float constant_bias = 0.0f;
		/// Nonzero for cascades whose rays march from behind the near plane
		uint32_t directional = 0;
	};

	struct VoxelPipelineSet {
		uint32_t view_mask = 1;

		/// Indexed [cull front][exact depth] since mirroring flips the winding
		std::array<std::array<VulkanPipeline, 2>, 2> pipelines;
	};

	/// Mirrors voxel_shadow_mesh_common.slang ShadowMeshPush
	struct VoxelMeshShadowPush {
		uint32_t instance_index = 0;
		uint32_t view_index = 0;
		uint32_t view_count = 1;
		uint32_t directional = 0;
	};

	static constexpr uint32_t k_max_voxel_casters = 4096;
	/// Throttles the over limit warning
	std::chrono::steady_clock::time_point m_last_caster_limit_warning;

	struct LayerGroup {
		uint32_t base_layer = 0;
		uint32_t layer_count = 1;

		/// Never 0 since that disables multiview
		uint32_t view_mask = 1;

		std::optional<vk::raii::ImageView> view;

		bool dirty = true;

		std::optional<uint64_t> signature;

		/// Resting voxel casters of the static group while signature holds its views
		std::optional<uint64_t> caster_signature;

		/// Voxel storage the static group last matched
		std::optional<uint64_t> voxel_generation;

		/// Static renders so slot signatures tell the copies apart
		uint64_t revision = 0;
	};

	struct ShadowMap {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> array_view;
		std::vector<LayerGroup> groups;
		sync::ImageState state;
		uint32_t resolution = 0;
		uint32_t layer_count = 0;
	};

	struct PipelineSet {
		uint32_t view_mask = 1;
		VulkanPipeline pipeline;
	};

	struct FrameTarget {
		ShadowMap cascades;
		ShadowMap punctual;
		FrameResources ubo;
	};

	/// Resting voxel casters copied under the moving casters of each slot
	struct StaticMaps {
		ShadowMap cascades;
		ShadowMap punctual;
	};

	void createResources(const VulkanCore& core);

	void createShadowMap(
	    const VulkanCore& core, ShadowMap& map, uint32_t resolution, std::span<const uint32_t> group_layers,
	    vk::ImageUsageFlags usage, std::string_view debug_name
	);

	[[nodiscard]]
	auto pipelineSetFor(uint32_t view_mask) const -> const PipelineSet*;

	void createVoxelResources(const VulkanCore& core);

	/// @returns the voxel instances written for this slot
	[[nodiscard]]
	auto prepareVoxels(uint32_t frame_index) -> uint32_t;

	[[nodiscard]]
	auto voxelPipelineSetFor(uint32_t view_mask) const -> const VoxelPipelineSet*;

	/// Task and mesh shader voxel casters which share the voxel shadow descriptor sets
	void createVoxelMeshResources(const VulkanCore& core);

	[[nodiscard]]
	auto voxelMeshPipelineFor(uint32_t view_mask) const -> const VulkanPipeline*;

	void recordMap(vk::CommandBuffer cmd, ShadowMap& map, ShadowMap& static_map, uint32_t frame_index, bool directional);

	[[nodiscard]]
	static auto selectShadowFormat(const VulkanCore& core) -> vk::Format;

	const VulkanCore* m_core = nullptr;
	vk::Format m_format = vk::Format::eUndefined;

	ShaderLayout m_shader_layout;

	/// VulkanPipeline is not movable
	std::array<PipelineSet, 2> m_pipeline_sets;

	Stats m_stats;
	std::vector<VoxelCaster> m_voxel_eligible;

	vk::raii::Sampler m_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;
	std::vector<FrameTarget> m_targets;
	StaticMaps m_static;

	ShaderLayout m_voxel_layout;
	std::array<VoxelPipelineSet, 2> m_voxel_pipeline_sets;
	std::vector<vk::raii::DescriptorSet> m_voxel_shadow_sets;
	std::vector<vk::raii::DescriptorSet> m_voxel_storage_sets;
	std::vector<vma::raii::Buffer> m_voxel_instance_buffers;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_voxel_bound_storage;
	bool m_voxels_ready = false;

	/// Depth units of the rasterizer constant bias since SV_Depth skips it
	float m_voxel_constant_bias = 0.0f;

	uint32_t m_voxel_instance_count = 0;

	struct SettledCaster {
		glm::mat4 model {1.0f};
		glm::mat4 inverse_model {1.0f};
		uint64_t moved_frame = 0;
		uint64_t seen_frame = 0;
	};

	std::unordered_map<uint64_t, SettledCaster> m_settled_casters;
	uint64_t m_caster_frame = 0;
	std::vector<glm::mat4> m_caster_models;
	std::vector<uint8_t> m_caster_moving;

	std::vector<uint32_t> m_voxel_draw_order;

	ShaderLayout m_voxel_mesh_layout;
	/// Matches m_voxel_pipeline_sets by index
	std::array<VulkanPipeline, 2> m_voxel_mesh_pipelines;
	PFN_vkCmdDrawMeshTasksEXT m_draw_mesh_tasks = nullptr;
	std::array<uint32_t, 3> m_max_task_groups {};
	uint32_t m_max_task_group_total = 0;

	std::vector<vk::ImageCopy> m_static_copies;
};

}
