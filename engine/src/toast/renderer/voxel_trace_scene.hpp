/// @file voxel_trace_scene.hpp
/// @author dario
/// @date 21/10/2026

#pragma once

#include "voxel_gpu_storage.hpp"
#include "vulkan_common.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>

namespace renderer {
class VulkanCore;
class RayTracingScene;
class VoxelBlasCache;

/// Voxel volumes every marched ray walks in one order shared by scene_trace.slang and the ray query instances
/// Without ray queries rays walk a camera centred cell grid over the same list
class VoxelTraceScene {
public:
	struct Volume {
		VoxelInstanceGpu instance;
		glm::vec4 bounds {0.0f};
		bool glass = false;
		/// Voxel space box is [0 dims)
		glm::uvec3 voxel_dims {0};
	};

	static constexpr uint32_t k_max_volumes = 4096;
	static constexpr uint32_t k_grid_dim = 32;
	static constexpr uint32_t k_max_cells_per_volume = 64;
	static constexpr uint32_t k_max_entries = 131072;
	/// Mirrors scene_trace.slang kTraceHeaderWords
	static constexpr uint32_t k_header_words = 16;
	static constexpr uint32_t k_grid_words =
	    k_header_words + (2 * k_grid_dim * k_grid_dim * k_grid_dim) + k_max_entries + (2 * k_max_volumes);

	/// Mirrors scene_trace.slang kTraceMask while mesh instances use 0x01
	static constexpr uint8_t k_mask_volume = 0x02;
	static constexpr uint8_t k_mask_glass = 0x04;

	VoxelTraceScene(const VulkanCore& core, uint32_t frames_in_flight);

	/// Render thread before the acceleration structure builds and every pass that marches voxels
	void update(uint32_t frame_index, std::span<const Volume> volumes, glm::vec3 camera_position, float cell_size);

	/// Builds the shared unit box once and adds the brick BLASes of @p bricks or the unit box per volume of the last update
	/// @return false while the unit box is not usable yet
	auto addRayQueryInstances(
	    vk::CommandBuffer cmd, uint32_t frame_index, RayTracingScene& scene, const VoxelBlasCache* bricks,
	    const VoxelGpuStorage* storage
	) -> bool;

	[[nodiscard]]
	auto volumeBuffer(uint32_t frame_index) const -> vk::Buffer {
		return *m_frames[frame_index].volumes;
	}

	[[nodiscard]]
	auto boundsBuffer(uint32_t frame_index) const -> vk::Buffer {
		return *m_frames[frame_index].bounds;
	}

	[[nodiscard]]
	auto gridBuffer(uint32_t frame_index) const -> vk::Buffer {
		return *m_frames[frame_index].grid;
	}

	/// Mirrors scene_trace.slang gTraceBrickBases
	[[nodiscard]]
	auto brickBaseBuffer(uint32_t frame_index) const -> vk::Buffer {
		return *m_frames[frame_index].brick_bases;
	}

	[[nodiscard]]
	auto volumeCount() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_volumes.size());
	}

private:
	struct FrameResources {
		vma::raii::Buffer volumes = nullptr;
		vma::raii::Buffer bounds = nullptr;
		vma::raii::Buffer grid = nullptr;
		vma::raii::Buffer brick_bases = nullptr;
	};

	void writeGrid(FrameResources& frame, glm::vec3 camera_position, float cell_size);
	void createUnitBox();

	const VulkanCore* m_core = nullptr;
	std::vector<FrameResources> m_frames;
	std::vector<Volume> m_volumes;
	std::vector<uint32_t> m_cell_counts;
	std::vector<uint32_t> m_cell_cursor;

	std::optional<vma::raii::Buffer> m_box_aabb;
	std::optional<vma::raii::Buffer> m_box_storage;
	std::optional<vma::raii::Buffer> m_box_scratch;
	vk::raii::AccelerationStructureKHR m_box = nullptr;
	vk::DeviceAddress m_box_address = 0;
	bool m_box_built = false;
};

}
