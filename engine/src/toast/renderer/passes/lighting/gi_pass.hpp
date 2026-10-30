/// @file gi_pass.hpp
/// @author dario
/// @date 22/10/2026

#pragma once

#include "toast/renderer/core/gpu_sync.hpp"
#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/passes/post_process_pass_base.hpp"
#include "toast/renderer/resources/post_process_target.hpp"
#include "toast/renderer/resources/scene_descriptor_set.hpp"
#include "toast/renderer/shaders/shader_layout.hpp"
#include "toast/renderer/shaders/vulkan_pipeline.hpp"

#include <array>
#include <chrono>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;
class VoxelGpuStorage;

class GiPass : public IPostProcessPass {
public:
	static constexpr uint32_t k_max_cascades = 6;
	/// c0 probes and every cascade above holds a quarter down to k_min_capacity
	static constexpr uint32_t k_base_capacity = 65536;
	static constexpr uint32_t k_min_capacity = 1024;
	/// Shared by every cascade and handed out top cascade first
	static constexpr uint32_t k_deposit_slots = 1u << 23;
	/// Mirrors gi_common kGiAtlasTilesPerRow and kGiAtlasRows and caps the c0 deposit probes at one tile each
	static constexpr uint32_t k_atlas_tiles_per_row = 256;
	static constexpr uint32_t k_atlas_rows = 128;
	static constexpr uint32_t k_atlas_tiles = k_atlas_tiles_per_row * k_atlas_rows;
	static constexpr uint32_t k_max_points = 65536;

	struct Stats {
		uint32_t cascades = 0;
		std::array<uint32_t, k_max_cascades> probes {};
		/// Probes seen within gi_common kGiKeepFrames and the rest are carried without storage
		std::array<uint32_t, k_max_cascades> seen {};
		std::array<uint32_t, k_max_cascades> capacity {};
		std::array<uint32_t, k_max_cascades> deposits {};
		std::array<uint32_t, k_max_cascades> deposit_capacity {};
		/// Mean rays of history behind one direction of a deposit probe
		std::array<float, k_max_cascades> depth {};
		uint32_t table_full = 0;
		uint32_t pool_full = 0;
		uint32_t deposit_full = 0;
		uint32_t rays = 0;
		uint32_t slots = 0;
		uint32_t moved = 0;
		uint32_t appeared = 0;
		uint32_t edits = 0;
		/// Meters including bodies under the motion threshold so physics jitter shows up
		float largest_motion = 0.0f;
		uint32_t invalidated = 0;
		uint32_t fresh = 0;
		float blend = 0.0f;
		/// Hit points the last trace stored for the secondary cache and the ones past the buffer
		uint32_t points = 0;
		uint32_t points_dropped = 0;
		float point_share = 0.0f;
		uint32_t quality = 0;
		uint32_t ray_step = 0;
		uint32_t update_interval = 0;
		uint32_t point_budget = 0;
	};

	GiPass(const VulkanCore& core, vk::Format scene_format, vk::Extent2D extent);

	[[nodiscard]]
	auto name() const -> std::string_view override {
		return "Radiance cascades";
	}

	auto record(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view) -> vk::ImageView override;

	void onResize(vk::Extent2D extent) override;

	/// A few frames old since it is read back once the fence of its frame passed
	[[nodiscard]]
	auto stats() const -> Stats;

	/// Set 0 buffers lighting samples by gi_sample binding name
	[[nodiscard]]
	auto sceneBuffer(std::string_view name) const -> vk::Buffer;

	[[nodiscard]]
	auto irradianceView() const -> vk::ImageView {
		return m_atlas_view.has_value() ? **m_atlas_view : vk::ImageView {};
	}

	[[nodiscard]]
	auto irradianceSampler() const -> vk::Sampler {
		return *m_linear_sampler;
	}

private:
	/// Mirrors gi_common GiFrame
	struct FrameData {
		glm::mat4 inverse_view_projection {1.0f};
		glm::vec4 camera {0.0f};
		glm::vec4 lod {0.0f};
		glm::uvec4 frame {0u};
		glm::uvec4 viewport {0u};
		glm::uvec4 pool {0u};
		glm::vec4 rays {0.0f};
		glm::vec4 jitter {0.0f};
		glm::uvec4 secondary {0u};
		glm::vec4 limits {0.0f};
		std::array<glm::uvec4, k_max_cascades> cascades {};
		std::array<glm::uvec4, k_max_cascades> deposits {};
		glm::uvec4 queues {0u};
	};

	/// Mirrors gi_common kGiCounter layout with counts per parity then table full pool full and seen per cascade
	static constexpr uint32_t k_counter_words = 44;
	static constexpr uint32_t k_counter_table_full = 16;
	static constexpr uint32_t k_counter_pool_full = 17;
	static constexpr uint32_t k_counter_seen = 18;
	static constexpr uint32_t k_counter_deposit_full = 24;
	static constexpr uint32_t k_counter_rays = 25;
	static constexpr uint32_t k_counter_slots = 26;
	static constexpr uint32_t k_counter_invalidated = 27;
	static constexpr uint32_t k_counter_points = 28;
	static constexpr uint32_t k_counter_points_dropped = 30;
	static constexpr uint32_t k_counter_fresh = 31;
	/// Sixteenths of the history rays of every 64th bucket per cascade
	static constexpr uint32_t k_counter_depth = 32;
	/// Pixels with a deposit that may exceed the spawn list
	static constexpr uint32_t k_counter_spawn = 38;
	/// Hits queued by pixel rays
	static constexpr uint32_t k_counter_pixel_hits = 39;
	/// Hits queued by point rays
	static constexpr uint32_t k_counter_point_hits = 40;
	/// Mirrors gi_probes gDirty and past it every extra region grows the last box
	static constexpr uint32_t k_max_dirty = 64;

	struct Cascade {
		uint32_t table_offset = 0;
		uint32_t table_size = 0;
		uint32_t pool_offset = 0;
		uint32_t capacity = 0;
		uint32_t deposit_offset = 0;
		uint32_t deposit_capacity = 0;
		uint32_t directions = 0;
	};

	/// The cost knobs of renderer.gi.quality
	struct Quality {
		uint32_t level = 0;
		uint32_t step = 2;
		uint32_t cascades = 5;
		float spacing = 0.1f;
		float lod_distance = 4.0f;
		bool multibounce = true;
		uint32_t points = k_max_points;
		uint32_t interval = 1;
	};

	/// What decides a probe key so changing it drops every carried probe
	struct KeySettings {
		float spacing = 0.0f;
		float lod_distance = 0.0f;
		[[nodiscard]]
		auto operator==(const KeySettings&) const -> bool = default;
	};

	void createLayout();
	void createBuffers(const VulkanCore& core);
	auto createPipelines(const VulkanCore& core) -> bool;
	void readStats(uint32_t frame_index);
	void bindProbeSet(uint32_t frame_index);
	auto bindRaySets(uint32_t frame_index, vk::ImageView scene_view) -> bool;
	/// False when the ray sets could not bind so nothing was traced
	auto recordTrace(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t spawn_x, uint32_t spawn_y, vk::ImageView scene_view)
	    -> bool;
	void recordShade(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t rays);
	void recordMerge(vk::CommandBuffer cmd, uint32_t frame_index, uint32_t cascades);
	void recordChange(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView scene_view);
	/// Sized to the viewport so it is rebuilt on resize while the device is idle
	void createChange(const VulkanCore& core);
	auto resolveQuality() const -> Quality;
	/// Every rendered frame so the boxes of skipped GI frames still reach the next one
	void gatherDirty();
	/// @return false when the edits are no longer known so every probe must drop its history
	auto writeDirty(uint32_t frame_index) -> bool;
	auto recordDebug(vk::CommandBuffer cmd, uint32_t frame_index, vk::ImageView source_view, uint32_t cascade, uint32_t mode)
	    -> vk::ImageView;

	const VulkanCore* m_core = nullptr;
	vk::Format m_scene_format = vk::Format::eUndefined;
	vk::Extent2D m_extent {};

	std::array<Cascade, k_max_cascades> m_cascades {};
	uint32_t m_table_total = 0;
	uint32_t m_pool_total = 0;
	uint32_t m_deposit_total = 0;

	ShaderLayout m_probe_layout;
	VulkanPipeline m_clear_pipeline;
	VulkanPipeline m_carry_pipeline;
	VulkanPipeline m_pixel_pipeline;
	VulkanPipeline m_point_pipeline;
	VulkanPipeline m_parent_pipeline;
	VulkanPipeline m_compact_pipeline;
	VulkanPipeline m_keep_pipeline;
	VulkanPipeline m_slots_pipeline;
	VulkanPipeline m_deposit_clear_pipeline;
	VulkanPipeline m_link_pipeline;
	VulkanPipeline m_count_pipeline;
	VulkanPipeline m_point_count_pipeline;
	VulkanPipeline m_propagate_pipeline;
	VulkanPipeline m_offset_pipeline;

	/// gi_rays with the scene in set 0 the voxel storage and trace scene in set 1 and the probes in set 2
	ShaderLayout m_ray_layout;
	VulkanPipeline m_ray_pipeline;
	VulkanPipeline m_point_trace_pipeline;
	VulkanPipeline m_shade_pipeline;
	VulkanPipeline m_change_pipeline;
	SceneDescriptorSets m_ray_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_ray_storage_sets;
	std::vector<std::shared_ptr<const VoxelGpuStorage>> m_ray_storage_bound;
	std::vector<vk::raii::DescriptorSet> m_ray_sets;
	std::vector<std::array<vk::ImageView, 3>> m_ray_bound;
	/// Reflection keeps gVoxelInstances only when the trace code reads it
	bool m_ray_instances_bound = false;

	/// gi_merge with the scene in set 0 for the sky and the cascades in set 1
	ShaderLayout m_merge_layout;
	VulkanPipeline m_merge_pipeline;
	VulkanPipeline m_irradiance_pipeline;
	SceneDescriptorSets m_merge_scene_sets;
	std::vector<vk::raii::DescriptorSet> m_merge_sets;

	ShaderLayout m_debug_layout;
	VulkanPipeline m_debug_pipeline;
	PostProcessTarget m_debug_target;

	vk::raii::Sampler m_point_sampler = nullptr;
	vk::raii::Sampler m_linear_sampler = nullptr;

	std::optional<vma::raii::Buffer> m_table_keys;
	std::optional<vma::raii::Buffer> m_table_flags;
	std::optional<vma::raii::Buffer> m_table_previous;
	std::optional<vma::raii::Buffer> m_table_values;
	std::optional<vma::raii::Buffer> m_table_cells;
	std::optional<vma::raii::Buffer> m_pool;
	std::optional<vma::raii::Buffer> m_counters;
	std::optional<vma::raii::Buffer> m_deposit_pool;
	std::optional<vma::raii::Buffer> m_parent;
	std::optional<vma::raii::Buffer> m_ray_counts;
	std::optional<vma::raii::Buffer> m_deposits;
	std::optional<vma::raii::Buffer> m_deposit_slots;
	std::optional<vma::raii::Buffer> m_neighbours;
	std::optional<vma::raii::Buffer> m_merged;
	/// FrameData of the last merge so lighting next frame reads the frame its tables belong to
	std::optional<vma::raii::Buffer> m_published;
	/// Two halves by pool parity of gi_common GiHistory
	std::optional<vma::raii::Buffer> m_history;
	std::optional<vma::raii::Buffer> m_history_source;
	/// Mirrors gi_common GiPoint in two halves by pool parity
	std::optional<vma::raii::Buffer> m_points;
	/// Per pixel float4 of gi_rays changeMain
	std::optional<vma::raii::Buffer> m_change;
	/// Pixels with a deposit packed x | y << 16 sized to the most any preset spawns
	std::optional<vma::raii::Buffer> m_spawn;
	uint32_t m_spawn_capacity = 0;
	/// Off screen hits as uint4 with the pixel rays in the spawn capacity then the point rays in k_max_points
	std::optional<vma::raii::Buffer> m_hits;
	bool m_spawn_overflow_logged = false;
	bool m_spawn_step_logged = false;
	std::vector<vma::raii::Buffer> m_dirty;

	std::vector<Stats> m_dirty_stats;
	std::vector<glm::vec3> m_dirty_scratch;
	Stats m_dirty_pending;
	bool m_dirty_known = true;
	std::unordered_map<uint64_t, glm::mat4> m_model_scratch;
	uint64_t m_region_generation = 0;
	uint32_t m_skipped = 0;
	Quality m_quality;
	/// Kept in shader read layout between frames
	std::optional<vma::raii::Image> m_atlas;
	std::optional<vk::raii::ImageView> m_atlas_view;
	sync::ImageState m_atlas_state;
	std::vector<vma::raii::Buffer> m_frame_data;
	std::vector<vma::raii::Buffer> m_readback;
	std::vector<bool> m_readback_valid;
	/// Pool parity the counts in each readback belong to
	std::vector<uint32_t> m_readback_parity;
	/// Share of off screen hits each readback frame kept so the next share can size the demand
	std::vector<float> m_readback_point_share;
	std::vector<uint32_t> m_readback_point_budget;
	/// Spawn list capacity each readback frame was recorded with
	std::vector<uint32_t> m_readback_spawn_capacity;
	float m_point_share = 0.5f;

	std::vector<vk::raii::DescriptorSet> m_probe_sets;
	std::vector<std::array<vk::ImageView, 2>> m_probe_bound;
	std::vector<vk::raii::DescriptorSet> m_debug_sets;
	std::vector<std::array<vk::ImageView, 3>> m_debug_bound;

	bool m_published_valid = false;
	uint32_t m_frame = 0;
	std::chrono::steady_clock::time_point m_last_record;
	float m_blend = 0.05f;
	std::optional<KeySettings> m_key_settings;

	mutable std::mutex m_stats_mutex;
	Stats m_stats;
};

}
