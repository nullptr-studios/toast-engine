/**
 * @file skinned_blas_pool.hpp
 * @author dario
 * @date 13/08/2026
 */

#pragma once

#include "vulkan_common.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace renderer {
class VulkanCore;
class VulkanMesh;

class SkinnedBlasPool {
public:
	explicit SkinnedBlasPool(const VulkanCore& core);

	void beginFrame();

	auto recordFor(
	    vk::CommandBuffer cmd, uint64_t node_uid, const VulkanMesh& mesh, vk::Buffer posed_vertices, uint32_t posed_vertex_offset
	) -> vk::DeviceAddress;

	void endFrame();

	[[nodiscard]]
	auto addressFor(uint64_t node_uid) const -> vk::DeviceAddress;

	[[nodiscard]]
	auto getRecordedCount() const noexcept -> uint32_t {
		return m_recorded;
	}

private:
	static constexpr uint64_t k_retire_frames = 8;

	struct Entry {
		const VulkanMesh* mesh = nullptr;
		uint32_t vertex_count = 0;
		uint32_t primitive_count = 0;

		std::optional<vma::raii::Buffer> blas_buffer;
		std::optional<vma::raii::Buffer> scratch;
		vk::raii::AccelerationStructureKHR blas = nullptr;
		vk::DeviceAddress address = 0;
		vk::DeviceAddress scratch_address = 0;

		bool built = false;
		uint64_t last_used_frame = 0;
	};

	auto ensureEntry(uint64_t node_uid, const VulkanMesh& mesh) -> Entry*;

	const VulkanCore* m_core = nullptr;
	std::unordered_map<uint64_t, Entry> m_entries;

	uint64_t m_frame = 0;
	uint32_t m_recorded = 0;

	/// From the RAII device dispatcher since the vulkan-hpp static loader has no extension entry points
	PFN_vkCmdBuildAccelerationStructuresKHR m_build_acceleration_structures = nullptr;
};

}
