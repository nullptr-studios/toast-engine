/**
 * @file voxel_gpu_storage.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "voxel_change_history.hpp"
#include "vulkan_resource_base.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
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

class VoxelGpuSections;

/// Immutable scene version and versions from small carves share buffers patched in place
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

	/// New section buffers that a VoxelSceneUpload fills
	VoxelGpuStorage(std::vector<uint64_t> node_rids, std::vector<glm::uvec3> brick_dims);

	/// Ready at once over the buffers of @p previous and needs every patch up to @p patch_sequence applied
	VoxelGpuStorage(
	    std::vector<uint64_t> node_rids, std::vector<glm::uvec3> brick_dims, const VoxelGpuStorage& previous,
	    uint64_t patch_sequence
	);

	/// Unique per storage so a cache keyed on it notices every upload
	[[nodiscard]]
	auto generation() const noexcept -> uint64_t {
		return m_generation;
	}

	[[nodiscard]]
	auto recordIndexOf(uint64_t node_rid) const -> std::optional<uint32_t>;

	[[nodiscard]]
	auto recordBrickDims(uint32_t index) const -> glm::uvec3 {
		return index < m_brick_dims.size() ? m_brick_dims[index] : glm::uvec3(0);
	}

	[[nodiscard]]
	auto recordCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_node_rids.size());
	}

	[[nodiscard]]
	auto buffer(Section section) const -> vk::Buffer;

	[[nodiscard]]
	auto size(Section section) const -> vk::DeviceSize;

	/// Tick thread on a ready storage and nullopt when the pool outgrew the buffers or staging failed
	[[nodiscard]]
	auto queuePatch(const VulkanCore& core, const voxel::gpu::ScenePatch& patch) const -> std::optional<uint64_t>;

	/// Render thread before anything reads the sections and @p retired must outlive the frame
	void recordPendingPatches(vk::CommandBuffer cmd, std::vector<std::shared_ptr<void>>& retired) const;

	[[nodiscard]]
	auto nodeRid(uint32_t index) const -> uint64_t {
		return index < m_node_rids.size() ? m_node_rids[index] : 0;
	}

	/// @note Main thread only and before the upload is queued
	void setDebugInfo(VoxelStorageDebugInfo info) { m_debug = std::move(info); }

	[[nodiscard]]
	auto debugInfo() const -> const VoxelStorageDebugInfo& {
		return m_debug;
	}

	/// @note Main thread only and before the upload is queued
	void setChangeHistory(VoxelChangeHistory history) { m_history = std::move(history); }

	[[nodiscard]]
	auto changeHistory() const -> const VoxelChangeHistory& {
		return m_history;
	}

	/// Per record grid indices of its glass bricks
	/// @note Main thread only and before the upload is queued
	void setGlassBricks(std::vector<std::vector<uint32_t>> bricks) { m_glass_bricks = std::move(bricks); }

	[[nodiscard]]
	auto glassBricks(uint32_t record) const -> std::span<const uint32_t> {
		return record < m_glass_bricks.size() ? std::span<const uint32_t>(m_glass_bricks[record]) : std::span<const uint32_t> {};
	}

private:
	friend class VoxelSceneUpload;

	std::vector<uint64_t> m_node_rids;
	std::vector<glm::uvec3> m_brick_dims;

	std::shared_ptr<VoxelGpuSections> m_sections;

	/// Last in place patch this version needs and 0 for none
	uint64_t m_patch_sequence = 0;

	uint64_t m_generation = 0;

	VoxelStorageDebugInfo m_debug;

	VoxelChangeHistory m_history;

	std::vector<std::vector<uint32_t>> m_glass_bricks;
};

/// Mirrors voxel_dda.slang VoxelInstance
struct VoxelInstanceGpu {
	glm::mat4 voxel_to_world {1.0f};
	glm::mat4 world_to_voxel {1.0f};
	glm::mat4 previous_voxel_to_world {1.0f};
	uint32_t record_index = 0;
	uint32_t pad0 = 0;
	uint32_t flags = 0;
	/// First global coarse cell of this instance in the VoxelPass cell cull
	uint32_t cell_base = 0;
};

static_assert(sizeof(VoxelInstanceGpu) == 208, "VoxelInstanceGpu is mirrored by voxel_dda.slang VoxelInstance");

/// Bit 0 of VoxelInstanceGpu::flags with the meaning of VulkanRenderer::k_instance_no_motion
inline constexpr uint32_t k_voxel_instance_no_motion = 1u << 0;

/// Bit 1 of VoxelInstanceGpu::flags so voxel.slang flips the box winding
inline constexpr uint32_t k_voxel_instance_mirrored = 1u << 1;

/// Voxel (x y z) occupies [x x + 1] in the space the instance maps from
[[nodiscard]]
auto makeVoxelInstance(
    const glm::mat4& model, const glm::mat4& inverse_model, const glm::mat4& previous_model, bool has_previous,
    uint32_t record_index
) -> VoxelInstanceGpu;

/// Bindings 0 to 5 of set 1 in voxel_dda.slang order
/// @note The set must not be in use by a frame still in flight
void writeVoxelStorageDescriptors(const vk::raii::Device& device, vk::DescriptorSet set, const VoxelGpuStorage& storage);

class VoxelSceneUpload : public PendingResourceUpload {
public:
	VoxelSceneUpload(std::shared_ptr<VoxelGpuStorage> storage, std::shared_ptr<const VoxelPackedScene> packed);

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	void recordAcquire(sync::BarrierBatch& batch) override;

	auto resource() -> IVulkanResource* override { return m_storage.get(); }

private:
	std::shared_ptr<VoxelGpuStorage> m_storage;

	std::shared_ptr<const VoxelPackedScene> m_packed;

	bool m_released = false;

	vma::raii::Buffer m_staging = nullptr;
	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_offsets {};

	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_bytes {};
};

}
