/**
 * @file voxel_gpu_storage.hpp
 * @author dario
 * @date 13/09/2026
 */

#pragma once
#include "vulkan_resource_base.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <toast/voxel/gpu_layout.hpp>
#include <vector>

namespace renderer {

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

private:
	friend class VoxelSceneUpload;

	std::vector<uint64_t> m_node_uids;
	std::vector<glm::uvec3> m_brick_dims;

	std::array<std::optional<vma::raii::Buffer>, k_section_count> m_buffers;
	std::array<vk::DeviceSize, k_section_count> m_sizes {};
};

class VoxelSceneUpload : public PendingResourceUpload {
public:
	VoxelSceneUpload(
	    std::shared_ptr<VoxelGpuStorage> storage, toast::voxel::gpu::PackedPool pool, toast::voxel::gpu::PackedScene scene
	);

	void build(const VulkanCore& core) override;

	void record(vk::CommandBuffer cmd) override;

	auto resource() -> IVulkanResource* override { return m_storage.get(); }

private:
	std::shared_ptr<VoxelGpuStorage> m_storage;

	toast::voxel::gpu::PackedPool m_pool;
	toast::voxel::gpu::PackedScene m_scene;

	vma::raii::Buffer m_staging = nullptr;
	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_offsets {};

	std::array<vk::DeviceSize, VoxelGpuStorage::k_section_count> m_bytes {};
};

}
