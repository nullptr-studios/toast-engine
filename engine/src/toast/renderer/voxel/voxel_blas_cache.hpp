/// @file voxel_blas_cache.hpp
/// @author dario
/// @date 25/10/2026

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"
#include "toast/renderer/voxel/voxel_gpu_storage.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace renderer {
class VulkanCore;

/// One AABB per occupied brick per record where carving keeps a built BLAS correct and only a newly occupied brick rebuilds
/// @note Render thread only apart from stats()
class VoxelBlasCache {
public:
	/// Records this small keep the unit box since marching them whole is already short
	static constexpr uint32_t k_unit_box_bricks = 8;
	/// Brick coordinates gTraceBricks holds for every BLAS together
	static constexpr uint32_t k_pool_bricks = 1u << 22;
	/// Frames a record goes unused before its BLAS is freed
	static constexpr uint64_t k_evict_frames = 240;
	/// Mirrors scene_trace.slang kTraceInstanceBricks and kTraceInstanceGlass
	static constexpr uint32_t k_instance_bricks = 1u << 23;
	static constexpr uint32_t k_instance_glass = 1u << 22;

	struct Lookup {
		vk::DeviceAddress opaque = 0;
		vk::DeviceAddress glass = 0;
		uint32_t opaque_base = 0;
		uint32_t glass_base = 0;
	};

	struct Stats {
		uint32_t blases = 0;
		uint64_t bricks = 0;
		uint64_t bytes = 0;
		uint32_t builds = 0;
		uint32_t built_bricks = 0;
		/// Usable BLASes that lost enough bricks to want a rebuild
		uint32_t stale = 0;
		/// Records on the unit box until their build fits the budget
		uint32_t waiting = 0;
		uint32_t pool_used = 0;
		uint32_t pool_full = 0;
	};

	VoxelBlasCache(const VulkanCore& core, uint32_t frames_in_flight);

	/// Before the TLAS build once the fence of @p frame_index was waited on
	/// @param records the volume records the trace scene instances this frame
	/// @param budget bricks built per frame though the first build always goes ahead
	void update(
	    vk::CommandBuffer cmd, uint32_t frame_index, const VoxelGpuStorage& storage, std::span<const uint32_t> records,
	    uint32_t budget
	);

	/// Null keeps the volume on the unit box
	[[nodiscard]]
	auto lookup(uint64_t node_rid) const -> const Lookup*;

	[[nodiscard]]
	auto poolBuffer() const -> vk::Buffer {
		return *m_pool;
	}

	[[nodiscard]]
	auto stats() const -> Stats;

private:
	struct Blas {
		vma::raii::Buffer storage = nullptr;
		vk::raii::AccelerationStructureKHR handle = nullptr;
		vk::DeviceAddress address = 0;
		uint32_t base = 0;
		uint32_t count = 0;
		vk::DeviceSize bytes = 0;
	};

	struct Entry {
		std::shared_ptr<const VoxelBrickList> built;
		std::optional<Blas> opaque;
		std::optional<Blas> glass;
		Lookup lookup;
		/// Hash of the last list checked against the built one
		uint64_t checked = 0;
		bool usable = false;
		bool stale = false;
		uint64_t last_used = 0;
	};

	struct FrameResources {
		std::optional<vma::raii::Buffer> staging;
		vk::DeviceSize staging_size = 0;
		std::optional<vma::raii::Buffer> scratch;
		vk::DeviceSize scratch_size = 0;
		/// Freed once this slot comes round again since older frames may still trace them
		std::vector<Blas> retired;
	};

	struct Job {
		uint64_t node_rid = 0;
		std::shared_ptr<const VoxelBrickList> list;
		bool required = false;
		size_t dead = 0;
	};

	void retire(FrameResources& frame, Entry& entry);
	auto allocate(uint32_t count) -> std::optional<uint32_t>;
	void release(uint32_t base, uint32_t count);
	auto createBlas(uint32_t count, vk::DeviceSize& scratch_size) -> std::optional<Blas>;

	const VulkanCore* m_core = nullptr;
	std::vector<FrameResources> m_frames;
	std::unordered_map<uint64_t, Entry> m_entries;
	std::vector<Job> m_jobs;

	vma::raii::Buffer m_pool = nullptr;
	/// Free runs of the pool as offset to length
	std::map<uint32_t, uint32_t> m_free;
	uint32_t m_pool_used = 0;
	uint64_t m_frame = 0;

	mutable std::mutex m_stats_mutex;
	Stats m_stats;
};

}
