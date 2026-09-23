/**
 * @file voxel_gpu_storage.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "voxel_change_history.hpp"
#include "vulkan_resource_base.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <toast/voxel/gpu_layout.hpp>
#include <vector>

namespace renderer {

struct VoxelPackedScene {
	voxel::gpu::PackedPool pool;
	voxel::gpu::PackedScene scene;
};

/// Immutable once the storage is published
struct VoxelStorageDebugInfo {
	struct BrickCensus {
		uint32_t uniform = 0;
		uint32_t shared = 0;
		uint32_t owned = 0;
	};

	/// Per record
	std::vector<std::string> node_names;
	std::vector<BrickCensus> bricks;

	uint64_t sequence = 0;
	double pack_ms = 0.0;
	uint32_t packed_slots = 0;
	uint32_t packed_palettes = 0;

	bool patched = false;
	uint32_t patched_bricks = 0;

	/// Only kept while a voxel debug view is active
	std::shared_ptr<const VoxelPackedScene> mirror;
};

class VoxelGpuStorage : public IVulkanResource {
public:
	/// In voxel_dda.slang binding order
	enum class Section : uint8_t {
		materials,
		occupancy,
		grids,
		coarse,
		palettes,
		records,
	};

	static constexpr size_t k_section_count = 6;

	VoxelGpuStorage(std::vector<uint64_t> node_uids, std::vector<glm::uvec3> brick_dims);

	/// Unique per storage so a cache keyed on it notices every upload
	[[nodiscard]]
	auto generation() const noexcept -> uint64_t {
		return m_generation;
	}

	[[nodiscard]]
	auto recordIndexOf(uint64_t node_uid) const -> std::optional<uint32_t>;

	[[nodiscard]]
	auto recordBrickDims(uint32_t index) const -> glm::uvec3 {
		return index < m_brick_dims.size() ? m_brick_dims[index] : glm::uvec3(0);
	}

	[[nodiscard]]
	auto recordCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_node_uids.size());
	}

	[[nodiscard]]
	auto buffer(Section section) const -> vk::Buffer;

	[[nodiscard]]
	auto size(Section section) const -> vk::DeviceSize {
		return m_sizes[static_cast<size_t>(section)];
	}

	[[nodiscard]]
	auto nodeUid(uint32_t index) const -> uint64_t {
		return index < m_node_uids.size() ? m_node_uids[index] : 0;
	}

	/// @note Main thread only and before the upload is queued
	void setDebugInfo(VoxelStorageDebugInfo info) { m_debug = std::move(info); }

	[[nodiscard]]
	auto debugInfo() const -> const VoxelStorageDebugInfo& {
		return m_debug;
	}

	/// @note Main thread only and before the upload is queued
	void setChangeHistory(VoxelChangeHistory history) { m_history = std::move(history); }

	/// Set once the copies that fill it are recorded so a patch may read from it
	void markRecorded() noexcept { m_recorded.store(true, std::memory_order_release); }

	[[nodiscard]]
	auto isRecorded() const noexcept -> bool {
		return m_recorded.load(std::memory_order_acquire);
	}

	[[nodiscard]]
	auto changeHistory() const -> const VoxelChangeHistory& {
		return m_history;
	}

private:
	friend class VoxelSceneUpload;
	friend class VoxelScenePatchUpload;

	std::vector<uint64_t> m_node_uids;
	std::vector<glm::uvec3> m_brick_dims;

	std::array<std::optional<vma::raii::Buffer>, k_section_count> m_buffers;
	std::array<vk::DeviceSize, k_section_count> m_sizes {};

	uint64_t m_generation = 0;

	VoxelStorageDebugInfo m_debug;

	VoxelChangeHistory m_history;

	std::atomic<bool> m_recorded {false};
};

/// Mirrors voxel_dda.slang VoxelInstance
struct VoxelInstanceGpu {
	glm::mat4 voxel_to_world {1.0f};
	glm::mat4 world_to_voxel {1.0f};
	uint32_t record_index = 0;
	uint32_t pad0 = 0;
	uint32_t pad1 = 0;
	uint32_t pad2 = 0;
};

static_assert(sizeof(VoxelInstanceGpu) == 144, "VoxelInstanceGpu is mirrored by voxel_dda.slang VoxelInstance");

/// Voxel (x y z) occupies [x x + 1] in the space the instance maps from
[[nodiscard]]
auto makeVoxelInstance(const glm::mat4& model, const glm::mat4& inverse_model, uint32_t record_index) -> VoxelInstanceGpu;

/// Bindings 0 to 5 of set 1 in voxel_dda.slang order
/// @note The set must not be in use by a frame still in flight
void writeVoxelStorageDescriptors(const vk::raii::Device& device, vk::DescriptorSet set, const VoxelGpuStorage& storage);

class VoxelSceneUpload : public PendingResourceUpload {
public:
	VoxelSceneUpload(std::shared_ptr<VoxelGpuStorage> storage, std::shared_ptr<const VoxelPackedScene> packed);

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	auto resource() -> IVulkanResource* override { return m_storage.get(); }

private:
	std::shared_ptr<VoxelGpuStorage> m_storage;

	std::shared_ptr<const VoxelPackedScene> m_packed;

	vma::raii::Buffer m_staging = nullptr;
	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_offsets {};

	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_bytes {};
};

/// Copies the previous storage and overwrites what changed so a carve does not pack or send the whole scene
class VoxelScenePatchUpload : public PendingResourceUpload {
public:
	using SectionBytes = std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count>;

	VoxelScenePatchUpload(
	    std::shared_ptr<VoxelGpuStorage> storage, std::shared_ptr<VoxelGpuStorage> source, voxel::gpu::ScenePatch patch,
	    SectionBytes section_bytes
	);

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	/// The copies read the source so they wait until its own copies are recorded
	[[nodiscard]]
	auto canRecord() const -> bool override;

	void finished() override;

	auto resource() -> IVulkanResource* override { return m_storage.get(); }

private:
	static constexpr size_t k_patched_sections = 4;

	std::shared_ptr<VoxelGpuStorage> m_storage;
	std::shared_ptr<VoxelGpuStorage> m_source;
	voxel::gpu::ScenePatch m_patch;
	SectionBytes m_section_bytes {};

	vma::raii::Buffer m_staging = nullptr;

	/// Materials then occupancy then grids then coarse
	std::array<vk::DeviceSize, k_patched_sections> m_staging_offsets {};
};

}
