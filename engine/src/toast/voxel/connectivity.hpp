/**
 * @file connectivity.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "brick.hpp"
#include "voxel_constants.hpp"
#include "voxel_volume.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <toast/export.hpp>
#include <vector>

namespace toast::voxel {

inline constexpr uint32_t k_no_component = 0xFFFFFFFFu;

/// @brief Edge or corner contact does not connect
[[nodiscard]]
inline auto brickComponents(const BrickOccupancy& brick) -> std::vector<BrickOccupancy> {
	if (isEmpty(brick)) {
		return {};
	}
	if (isFull(brick)) {
		return std::vector<BrickOccupancy>(1, brick);
	}

	std::vector<BrickOccupancy> out;
	BrickOccupancy remaining = brick;
	while (!isEmpty(remaining)) {
		// x & (~x + 1) isolates the lowest set bit
		BrickOccupancy region {};
		for (uint32_t z = 0; z < k_brick_dim; ++z) {
			if (remaining[z] != 0ull) {
				region[z] = remaining[z] & (~remaining[z] + 1ull);
				break;
			}
		}

		for (;;) {
			const BrickOccupancy grown = dilate(region) & remaining;
			if (grown == region) {
				break;
			}
			region = grown;
		}

		out.push_back(region);
		remaining = remaining & ~region;
	}
	return out;
}

struct BrickPiece {
	glm::ivec3 brick {0};
	BrickOccupancy voxels {};

	uint32_t component = 0;
};

/// @brief Labels are deterministic with pieces in brick order and components by first appearance
struct Connectivity {
	std::vector<BrickPiece> pieces;
	uint32_t component_count = 0;

	[[nodiscard]]
	auto componentOf(glm::ivec3 voxel) const -> uint32_t {
		const glm::ivec3 brick(voxel.x >> 3, voxel.y >> 3, voxel.z >> 3);
		const auto x = static_cast<uint32_t>(voxel.x & 7);
		const auto y = static_cast<uint32_t>(voxel.y & 7);
		const auto z = static_cast<uint32_t>(voxel.z & 7);
		for (const BrickPiece& piece : pieces) {
			if (piece.brick == brick && isSolid(piece.voxels, x, y, z)) {
				return piece.component;
			}
		}
		return k_no_component;
	}

	[[nodiscard]]
	auto voxelCount(uint32_t component) const -> uint32_t {
		uint32_t total = 0;
		for (const BrickPiece& piece : pieces) {
			if (piece.component == component) {
				total += popCount(piece.voxels);
			}
		}
		return total;
	}
};

[[nodiscard]]
TOAST_API auto analyseConnectivity(const Volume& volume) -> Connectivity;

}
