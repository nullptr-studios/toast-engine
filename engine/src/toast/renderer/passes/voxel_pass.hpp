/**
 * @file voxel_pass.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once

#include "../hiz_pyramid.hpp"
#include "../render_pass_base.hpp"
#include "../scene_descriptor_set.hpp"
#include "../shader_layout.hpp"
#include "../vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

/// Deferred so lighting runs once per visible voxel pixel not once per march
class VoxelPass : public IRenderPass {
public:
	VoxelPass(const VulkanCore& core, vk::Format scene_format, vk::Format depth_format, vk::Extent2D extent);

	[[nodiscard]]
	auto stage() const -> RenderStage override {
		return RenderStage::voxel_gbuffer;
	}

	/// @returns false when nothing draws so the caller skips both voxel scopes
	[[nodiscard]]
	auto prepare(uint32_t frame_index) -> bool;

	/// Outside any rendering scope after prepare() and before record()
	void recordCellCull(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D scene_extent);

	/// Inside the voxel G-buffer scope after prepare() for everything the previous pyramid did not hide
	void record(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t image_index) override;

	/// The renderer then runs recordOcclusion() and a second G-buffer scope for recordLate()
	[[nodiscard]]
	auto occlusionActive() const -> bool {
		return m_occlusion_active;
	}

	/// Outside any rendering scope with depth in depth_sampled
	void recordOcclusion(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D scene_extent);

	/// Inside a G-buffer scope that loads the early draws
	void recordLate(vk::CommandBuffer cmd, uint32_t frame_index);

	/// A depth only scope for recordQuadDepth() precedes each G-buffer scope except on the mesh shader path
	[[nodiscard]]
	auto nearQuadDepthPass() const -> bool {
		return m_near_voxels > 0.0f && !m_mesh_shading;
	}

	/// Inside a depth only scope before the G-buffer scope that shades the same quads
	void recordQuadDepth(vk::CommandBuffer cmd, uint32_t frame_index, bool late);

	/// Writes scene colour indirect and rounded normal while reading the G-buffer and depth
	void recordLighting(vk::CommandBuffer cmd, uint32_t frame_index, vk::Extent2D viewport);

	/// After the G-buffer and depth targets are recreated since a new view can reuse a destroyed handle value
	void onTargetsRecreated();

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Voxels";
	}

	static constexpr uint32_t k_max_instances = 4096;

private:
	/// Throttles the over limit warning
	std::chrono::steady_clock::time_point m_last_limit_warning;

	/// Mirrors voxel.slang PushConstants
	struct PushConstants {
		uint32_t instance_index = 0;
		uint32_t bucket = 0;
		uint32_t mode = 0;
		uint32_t quad_capacity = 0;
	};

	/// Mirrors voxel.slang kDrawVolume and kDrawCells
	static constexpr uint32_t k_draw_volume = 0;
	static constexpr uint32_t k_draw_cells = 1;

	/// Mirrors voxel_cells.slang CellCullPush
	struct CellCullPush {
		glm::mat4 view_projection {1.0f};
		glm::vec4 camera_near {0.0f};
		uint32_t total_cells = 0;
		uint32_t instance_count = 0;
		uint32_t phase = 0;
		uint32_t quad_capacity = 0;
		float near_voxels = 0.0f;
		uint32_t hiz_levels = 0;
		uint32_t hiz_width = 0;
		uint32_t hiz_height = 0;
	};

	/// Mirrors voxel_cells.slang HizView
	struct HizView {
		glm::mat4 view_projection {1.0f};
	};

	/// Mirrors voxel_dda.slang kCellBucketCount and the word layout after it
	static constexpr uint32_t k_cell_bucket_count = 17;
	static constexpr uint32_t k_late_list = k_cell_bucket_count;
	static constexpr uint32_t k_cell_draw_count = k_cell_bucket_count + 1;
	static constexpr uint32_t k_near_list = k_cell_draw_count;
	static constexpr uint32_t k_near_band_count = 4;
	static constexpr uint32_t k_retest_list = k_near_list + k_near_band_count;
	static constexpr uint32_t k_late_near_list = k_retest_list + 1;
	static constexpr uint32_t k_cell_list_count = k_late_near_list + 1;
	static constexpr uint32_t k_cell_offsets_word = k_cell_draw_count * 4;
	static constexpr uint32_t k_cell_cursors_word = k_cell_offsets_word + k_cell_list_count;
	static constexpr uint32_t k_near_dispatch_word = k_cell_cursors_word + k_cell_list_count;
	static constexpr uint32_t k_retest_dispatch_word = k_near_dispatch_word + 3;
	static constexpr uint32_t k_late_mesh_dispatch_word = k_retest_dispatch_word + 3;
	static constexpr uint32_t k_quad_draw_word = k_late_mesh_dispatch_word + 3;
	static constexpr uint32_t k_late_quad_draw_word = k_quad_draw_word + 4;
	static constexpr uint32_t k_near_task_word = k_late_quad_draw_word + 4;
	static constexpr uint32_t k_late_task_word = k_near_task_word + 3;
	static constexpr uint32_t k_occlusion_stats_word = k_late_task_word + 3;
	static constexpr uint32_t k_cell_args_words = k_occlusion_stats_word + 2;
	static constexpr uint32_t k_min_quad_capacity = 65536;
	/// Ten bits per axis in voxel_dda.slang packCell
	static constexpr uint32_t k_max_coarse_dim = 1024;
	static constexpr uint32_t k_min_cell_capacity = 4096;

	/// Per frame in flight
	struct CellBuffers {
		/// Instance index and packed cell per surviving cell
		std::optional<vma::raii::Buffer> cells;
		/// Count phase result per cell of the dispatch
		std::optional<vma::raii::Buffer> classes;
		/// Indirect commands then list offsets and cursors then the mesh dispatch and the quad draw
		std::optional<vma::raii::Buffer> args;
		uint32_t capacity = 0;
		/// Cell entry index and packed quad per meshed face run
		std::optional<vma::raii::Buffer> quads;
		uint32_t quad_capacity = 0;
		/// Quad draw vertex count for growing the quad buffer then the occlusion stats words
		std::optional<vma::raii::Buffer> readback;
		/// The early test ran when this slot last recorded so its stats describe occlusion
		bool measured = false;
		/// HizView for the early test
		std::optional<vma::raii::Buffer> hiz_view;
	};

	/// Mirrors voxel_lighting.slang LightingPushConstants
	struct LightingPushConstants {
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 camera_radius {0.0f};
		glm::vec4 viewport {0.0f};
	};

	void createCells(const VulkanCore& core);
	void ensureCellCapacity(uint32_t frame_index, uint32_t cells);
	void ensureQuadCapacity(uint32_t frame_index, uint32_t quads);
	/// The cull and mesh dispatches wrote these before the draws read them
	void afterCullWrites(vk::CommandBuffer cmd, uint32_t frame_index, sync::BarrierBatch& barriers);
	/// Vertex pulled quads from the compute mesher or task and mesh shaders from the near list itself
	void recordNearQuads(vk::CommandBuffer cmd, uint32_t frame_index, bool late, bool depth_only);
	/// After the last cull or mesh dispatch of the frame so the readback holds every quad
	void recordReadback(vk::CommandBuffer cmd, uint32_t frame_index, sync::BarrierBatch& barriers);
	/// Idles occlusion culling while the early test hides too little to pay for the late passes
	void updateOcclusionMode(uint32_t retest_cells, uint32_t listed_cells);
	void writeCellDescriptors(uint32_t frame_index);
	/// After the pyramid is created since its view changes with the depth target
	void writeHizDescriptors();
	void recordCellDraws(vk::CommandBuffer cmd, uint32_t frame_index);

	enum class Cull : uint8_t {
		back,
		front,
	};

	enum class Depth : uint8_t {
		/// SV_DepthGreaterEqual with the camera outside
		conservative,
		/// SV_Depth with the camera inside
		exact,
	};

	struct Draw {
		uint32_t instance = 0;
		Cull cull = Cull::back;
		Depth depth = Depth::conservative;
	};

	void createInstanceBuffers(const VulkanCore& core);
	void createDescriptors(const VulkanCore& core, const ShaderReflection& reflection);
	void createLighting(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	void bindStorage(uint32_t frame_index, const std::shared_ptr<const VoxelGpuStorage>& storage);

	const VulkanCore* m_core = nullptr;
	ShaderLayout m_shader_layout;

	/// Indexed [cull][depth]
	std::array<std::array<VulkanPipeline, 2>, 2> m_pipelines;

	SceneDescriptorSets m_scene_sets;

	std::vector<vk::raii::DescriptorSet> m_storage_sets;

	std::vector<vma::raii::Buffer> m_instance_buffers;

	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_bound_storage;

	std::vector<Draw> m_draws;

	ShaderLayout m_cell_layout;
	VulkanPipeline m_cell_pipeline;
	VulkanPipeline m_mesh_pipeline;
	VulkanPipeline m_quad_pipeline;
	VulkanPipeline m_quad_depth_pipeline;
	/// voxel_mesh.slang only on devices with mesh shaders
	VulkanPipeline m_mesh_quad_pipeline;
	PFN_vkCmdDrawMeshTasksIndirectEXT m_draw_mesh_tasks_indirect = nullptr;
	/// Near cells mesh in task and mesh shaders this frame instead of the compute mesher
	bool m_mesh_shading = false;
	std::vector<vk::raii::DescriptorSet> m_cell_sets;
	std::vector<CellBuffers> m_cell_buffers;

	bool m_cells_active = false;
	bool m_occlusion_active = false;

	enum class OcclusionMode : uint8_t {
		engaged,
		/// No pyramid and no late passes until a retry
		idle,
	};
	OcclusionMode m_occlusion_mode = OcclusionMode::engaged;
	/// Consecutive measured frames that hid less than k_occlusion_min_hidden
	uint32_t m_occlusion_low_frames = 0;
	/// Frames left before an idle period engages again to measure
	uint32_t m_occlusion_idle_frames = 0;
	/// The early test only trusts a pyramid built the frame before
	uint64_t m_frame_count = 0;
	uint64_t m_pyramid_frame = 0;
	uint32_t m_total_cells = 0;
	/// What recordCellCull() pushed so the late phase repeats it with only the phase and pyramid changed
	CellCullPush m_cull_push;
	HiZPyramid m_hiz;
	/// Cells closer than this many voxels are meshed this frame and 0 marches all of them
	float m_near_voxels = 0.0f;

	ShaderLayout m_lighting_layout;
	VulkanPipeline m_lighting_pipeline;
	SceneDescriptorSets m_lighting_scene_sets;

	vk::raii::Sampler m_point_sampler = nullptr;
	std::vector<vk::raii::DescriptorSet> m_gbuffer_sets;

	/// Albedo face normal material and depth last written into each set
	std::vector<std::array<vk::ImageView, 4>> m_bound_gbuffer;
};

}
