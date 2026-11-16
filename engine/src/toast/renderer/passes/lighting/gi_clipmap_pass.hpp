/// @file gi_clipmap_pass.hpp
/// @author dario
/// @date 10/11/2026

#pragma once

#include "toast/renderer/core/descriptor_cache.hpp"
#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/lighting/gi_clip_math.hpp"
#include "toast/renderer/lighting/gi_dirty_boxes.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/resources/scene_descriptor_set.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;
struct RenderFrame;

class GiClipmapPass : public IPostProcessPass {
public:
	/// Mirrors gi_clip_common kGiClipMaxLevels
	static constexpr uint32_t k_max_levels = 6;
	static constexpr uint32_t k_max_tail = 5;
	/// Mirrors gi_clip_common kGiClipFaces
	static constexpr uint32_t k_faces = 6;

#if defined(TOAST_DEV_TOOLS)
	/// What the last fill refilled and why
	struct Stats {
		uint32_t levels = 0;
		uint32_t edits = 0;
		/// Of GiDirtyBoxes::k_max_boxes where a full list merges the rest into its last box
		uint32_t edit_boxes = 0;
		uint32_t body_boxes = 0;
		bool history_lost = false;
		std::array<uint32_t, k_max_levels> boxes {};
		std::array<uint64_t, k_max_levels> cells {};
		std::array<uint64_t, k_max_levels> largest {};
		/// Cells left over for later frames
		std::array<uint64_t, k_max_levels> queued {};
		std::array<bool, k_max_levels> whole {};
	};

	[[nodiscard]]
	auto stats() const -> Stats;
#endif

	GiClipmapPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Radiance clipmap";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

	/// Copy of the last recorded frame with size.z 1 and zero while the clipmap holds nothing
	[[nodiscard]]
	auto publishedFrame() const -> vk::Buffer {
		return m_published.has_value() ? **m_published : vk::Buffer {};
	}

	/// Sampled face view at level * k_faces + face where unused levels repeat level 0 and a dummy stands in before the first fill
	[[nodiscard]]
	auto radianceViews() const -> std::array<vk::ImageView, k_max_levels * k_faces>;

	[[nodiscard]]
	auto ambientViews() const -> std::array<vk::ImageView, k_max_levels * k_faces>;

	[[nodiscard]]
	auto opacityViews() const -> std::array<vk::ImageView, k_max_levels>;

	/// Linear and repeat so the toroidal wrap filters across the texture edge
	[[nodiscard]]
	auto radianceSampler() const -> vk::Sampler {
		return *m_linear_sampler;
	}

private:
	/// Mirrors gi_clip_common GiClipFrame
	struct FrameData {
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 camera {0.0f};
		std::array<glm::vec4, k_max_levels> levels {};
		glm::uvec4 size {0u};
	};

	/// Lit outgoing radiance of one face direction written as R32 bits and sampled as B10G11R11
	struct Face {
		std::optional<vma::raii::Image> image;
		std::optional<vk::raii::ImageView> storage_view;
		std::optional<vk::raii::ImageView> sampled_view;
		std::vector<vk::raii::ImageView> mip_views;
		sync::ImageState state;
	};

	struct Level {
		std::optional<vma::raii::Image> material;
		std::optional<vk::raii::ImageView> material_view;
		sync::ImageState material_state;
		std::optional<vma::raii::Image> emission;
		std::optional<vk::raii::ImageView> emission_view;
		sync::ImageState emission_state;
		/// xyz opacity along each axis and w 1 when anything is solid
		std::optional<vma::raii::Image> opacity;
		std::optional<vk::raii::ImageView> opacity_view;
		std::optional<vk::raii::ImageView> opacity_all_view;
		std::vector<vk::raii::ImageView> opacity_mip_views;
		sync::ImageState opacity_state;
		std::array<Face, k_faces> faces;
		/// Bounce irradiance over pi each face gathered from the clipmap of the frame before
		std::array<Face, k_faces> ambient;
		glm::ivec3 origin {0};
		bool needs_full = true;
		/// Dirty cells past the fill budget that later frames refill first
		std::vector<gi_clip::Box> pending;
	};

	/// Dirty cells refilled per level each frame on top of the scroll so a blast spreads over frames
	static constexpr int64_t k_fill_budget_cells = 16384;

	auto createPipelines(const VulkanCore& core) -> bool;
	/// Waits for the device so no frame in flight still reads the old images
	void createLevels(uint32_t resolution, uint32_t levels);
	void markFull();
	void unpublish(vk::CommandBuffer cmd);
	void gatherBodies(const RenderFrame& frame);
	auto recordDebug(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, uint32_t level, uint32_t mode)
	    -> vk::ImageView;

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;
	vk::Extent2D m_extent {};

	/// gi_clip_fill with the level in set 0 and the voxel storage plus trace lists in set 1
	ShaderLayout m_fill_layout;
	VulkanPipeline m_fill_pipeline;
	std::vector<vk::raii::DescriptorSet> m_fill_sets;
	std::vector<vk::raii::DescriptorSet> m_storage_sets;
	DescriptorCache<std::shared_ptr<const VoxelGpuStorage>> m_storage_bound;

	/// gi_clip_inject with the engine scene in set 0 and the clipmap in set 1
	ShaderLayout m_inject_layout;
	VulkanPipeline m_inject_pipeline;
	SceneDescriptorSets m_inject_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_inject_sets;

	/// gi_clip_bounce with the engine scene in set 0 for the last frame clipmap and the ambient faces in set 1
	ShaderLayout m_bounce_layout;
	VulkanPipeline m_bounce_pipeline;
	SceneDescriptorSets m_bounce_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_bounce_sets;
	VulkanPipeline m_bounce_list_pipeline;
	/// A count then one record per exposed face of the level being bounced
	std::optional<vma::raii::Buffer> m_bounce_list;

	ShaderLayout m_down_layout;
	VulkanPipeline m_down_pipeline;
	std::vector<vk::raii::DescriptorSet> m_down_sets;
	VulkanPipeline m_mip_pipeline;
	std::vector<vk::raii::DescriptorSet> m_mip_sets;
	uint32_t m_tail = 0;
	uint32_t m_frame = 0;

	ShaderLayout m_debug_layout;
	VulkanPipeline m_debug_pipeline;
	PostProcessTarget m_debug_target;
	std::vector<vk::raii::DescriptorSet> m_debug_sets;
	DescriptorCache<std::array<vk::ImageView, 4>> m_debug_bound;

	vk::raii::Sampler m_point_sampler = nullptr;
	vk::raii::Sampler m_linear_sampler = nullptr;
	std::optional<vma::raii::Buffer> m_published;
	bool m_published_valid = false;
	std::optional<vma::raii::Image> m_dummy;
	std::optional<vk::raii::ImageView> m_dummy_view;
	std::vector<vma::raii::Buffer> m_frame_data;

	std::array<Level, k_max_levels> m_levels;
	uint32_t m_resolution = 0;
	uint32_t m_level_count = 0;
	float m_cell = 0.0f;

	/// Edits and lost history only since bodies are compared against the last fill below
	GiDirtyBoxes m_dirty_boxes;

	struct Body {
		glm::mat4 model {1.0f};
		/// xyz centre w radius in world space
		glm::vec4 bounds {0.0f};
	};

	struct TrackedBody {
		/// Where the clipmap last saw it
		Body filled;
		/// Where it was last frame
		Body seen;
		/// Frames it has moved since the last fill
		uint32_t flying = 0;
		/// Last gather that saw it so leavers are found without rebuilding the map
		uint64_t gather = 0;
	};

	static constexpr size_t k_max_body_boxes = 256;
	static constexpr uint32_t k_flying_refill_frames = 8;
	std::unordered_map<uint64_t, TrackedBody> m_bodies;
	uint64_t m_body_gather = 0;
	std::vector<glm::vec3> m_body_boxes;

#if defined(TOAST_DEV_TOOLS)
	mutable std::mutex m_stats_mutex;
	Stats m_stats;
#endif
};

}
