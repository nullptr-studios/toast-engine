/**
 * @file component_classification.hpp
 * @author Xein
 * @date 19 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "anchor_mask.hpp"

#include <glm/glm.hpp>
#include <toast/voxel/connectivity.hpp>

namespace physics {

inline constexpr uint32_t k_min_fragment_voxels = 4;

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

}
