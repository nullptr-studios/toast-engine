/// @file voxel_scene_patcher.hpp
/// @author dario
/// @date 22/09/2026

#pragma once

#include "toast/renderer/voxel/voxel_change_history.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/gpu_layout.hpp>
#include <vector>

namespace renderer {

/// Records stay in place so a removed volume leaves a dead record and a new one is appended
class VoxelScenePatcher {
public:
	struct Patched {
		voxel::gpu::ScenePatch patch;
		/// Per record and 0 for a dead one
		std::vector<uint64_t> record_rids;
		std::vector<glm::uvec3> record_dims;
	};

	/// A full pack is what the GPU will hold so later patches build on it
	void adopt(const voxel::gpu::PackedScene& scene, uint32_t pool_slots, std::vector<uint64_t> node_rids);

	/// @returns nullopt when only a full pack is worth it
	[[nodiscard]]
	auto tryPatch(
	    const voxel::BrickPool& pool, std::span<const voxel::gpu::SceneVolume> volumes, std::span<const uint64_t> node_rids,
	    std::span<const VolumeDelta> deltas
	) -> std::optional<Patched>;

	void reset();

	[[nodiscard]]
	auto valid() const noexcept -> bool {
		return m_valid;
	}

	[[nodiscard]]
	auto scene() const noexcept -> const voxel::gpu::PackedScene& {
		return m_scene;
	}

	[[nodiscard]]
	auto poolSlots() const noexcept -> uint32_t {
		return m_pool_slots;
	}

private:
	[[nodiscard]]
	auto paletteMatches(const voxel::gpu::VolumeRecord& record, const voxel::Palette& palette) const -> bool;

	/// In palette entries like VolumeRecord::palette_offset
	auto findOrAppendPalette(const voxel::Palette& palette) -> uint32_t;

	voxel::gpu::PackedScene m_scene;
	uint32_t m_pool_slots = 0;
	std::vector<uint64_t> m_node_rids;
	/// Grid words of dead records so enough of them makes a full pack compact the sections
	uint64_t m_dead_grid_words = 0;
	bool m_valid = false;
};

}
