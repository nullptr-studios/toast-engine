/// @file voxel_scene_patcher.hpp
/// @author dario
/// @date 22/09/2026

#pragma once

#include "voxel_change_history.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/gpu_layout.hpp>
#include <vector>

namespace renderer {

/// Remembers what the GPU copy of the voxel scene holds and turns small changes into patches of it
class VoxelScenePatcher {
public:
	/// A full pack is what the GPU will hold so later patches build on it
	void adopt(const voxel::gpu::PackedScene& scene, uint32_t pool_slots, std::vector<uint64_t> node_uids);

	/// @returns nullopt when only a full pack can describe the change
	[[nodiscard]]
	auto tryPatch(
	    const voxel::BrickPool& pool, std::span<const voxel::gpu::SceneVolume> volumes, std::span<const uint64_t> node_uids,
	    std::span<const VolumeDelta> deltas
	) -> std::optional<voxel::gpu::ScenePatch>;

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
	voxel::gpu::PackedScene m_scene;
	uint32_t m_pool_slots = 0;
	std::vector<uint64_t> m_node_uids;
	bool m_valid = false;
};

}
