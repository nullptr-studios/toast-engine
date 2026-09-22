/**
 * @file component_classification.hpp
 * @author Xein
 * @date 19 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "anchor_mask.hpp"
#include "physics_settings.hpp"

#include <glm/glm.hpp>
#include <toast/voxel/connectivity.hpp>

namespace physics {

inline auto minFragmentVoxels() -> uint32_t {
	return tunables().min_fragment_voxels;
}

enum class ComponentClass : uint8_t {
	anchored,
	detached,
	dropped,
};

[[nodiscard]]
auto classifyComponents(const voxel::Connectivity& c, glm::uvec3 brick_size, AnchorMask mask) -> std::vector<ComponentClass>;

struct DetachedComponent {
	uint32_t component = 0;
	std::vector<voxel::BrickPiece> pieces;
	uint32_t voxel_count = 0;
};

[[nodiscard]]
auto buildDetachedComponents(const voxel::Connectivity& c, std::span<const ComponentClass> classes, glm::uvec3 brick_size)
    -> std::vector<DetachedComponent>;

inline auto maxFragmentExtentBricks() -> int32_t {
	return tunables().max_fragment_extent_bricks;
}

[[nodiscard]]
auto splitBySpatialCompactness(std::vector<DetachedComponent> components, int32_t max_extent_bricks = maxFragmentExtentBricks())
    -> std::vector<DetachedComponent>;

}
