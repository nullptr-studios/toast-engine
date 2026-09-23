/// @file voxel_scene_patcher.cpp
/// @author dario
/// @date 22/09/2026

#include "voxel_scene_patcher.hpp"

#include <algorithm>
#include <utility>

namespace renderer {

void VoxelScenePatcher::adopt(const voxel::gpu::PackedScene& scene, uint32_t pool_slots, std::vector<uint64_t> node_uids) {
	m_scene = scene;
	m_pool_slots = pool_slots;
	m_node_uids = std::move(node_uids);
	m_valid = true;
}

auto VoxelScenePatcher::tryPatch(
    const voxel::BrickPool& pool, std::span<const voxel::gpu::SceneVolume> volumes, std::span<const uint64_t> node_uids,
    std::span<const VolumeDelta> deltas
) -> std::optional<voxel::gpu::ScenePatch> {
	using namespace voxel::gpu;

	// Same volumes in the same order since a record is found by its index
	if (!m_valid || deltas.size() != volumes.size() || !std::ranges::equal(node_uids, m_node_uids)) {
		return std::nullopt;
	}

	uint64_t dirty_bricks = 0;
	for (const VolumeDelta& delta : deltas) {
		if (delta.unknown) {
			return std::nullopt;
		}
		dirty_bricks += delta.bricks.size();
	}

	// A dirty brick costs its slot and a grid word and a full pack costs the whole pool
	constexpr uint64_t k_words_per_dirty_brick = k_material_words_per_brick + k_occupancy_words_per_brick + 1;
	const uint64_t full_words =
	    (static_cast<uint64_t>(m_pool_slots) * (k_material_words_per_brick + k_occupancy_words_per_brick)) + m_scene.grids.size();
	if (dirty_bricks * k_words_per_dirty_brick * 2 > full_words) {
		return std::nullopt;
	}

	if (!layoutMatches(m_scene, packLayout(volumes))) {
		return std::nullopt;
	}

	std::vector<VolumeDirty> dirty;
	for (size_t i = 0; i < deltas.size(); ++i) {
		if (!deltas[i].bricks.empty()) {
			dirty.push_back({.volume = static_cast<uint32_t>(i), .bricks = deltas[i].bricks});
		}
	}

	ScenePatch patch = computePatch(m_scene, m_pool_slots, pool, volumes, dirty);
	m_pool_slots = patch.pool_slots;
	return patch;
}

void VoxelScenePatcher::reset() {
	m_scene = {};
	m_pool_slots = 0;
	m_node_uids.clear();
	m_valid = false;
}

}
