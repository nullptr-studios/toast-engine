/**
 * @file voxel_smash.hpp
 * @author Dario
 * @date 03 Oct 2026
 */

#pragma once

#include "component_classification.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <toast/voxel/brick.hpp>
#include <toast/voxel/connectivity.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <utility>
#include <vector>

namespace physics {

inline constexpr float k_no_floor = -1.0e30f;

[[nodiscard]]
inline auto segmentBoxDistanceSquared(const glm::vec3& from, const glm::vec3& to, const glm::vec3& center, float half_extent)
    -> float {
	const auto squared = [&](float t) {
		const glm::vec3 gap = glm::max(glm::abs(glm::mix(from, to, t) - center) - glm::vec3(half_extent), glm::vec3(0.0f));
		return glm::dot(gap, gap);
	};

	float low = 0.0f;
	float high = 1.0f;
	for (int i = 0; i < 32; ++i) {
		const float first = low + ((high - low) / 3.0f);
		const float second = high - ((high - low) / 3.0f);
		if (squared(first) < squared(second)) {
			high = second;
		} else {
			low = first;
		}
	}
	return squared(0.5f * (low + high));
}

struct SmashVolume {
	glm::vec3 axis_start {};
	glm::vec3 axis_end {};
	glm::vec3 sweep {};
	float reach = 0.0f;
	glm::vec3 up {0.0f, 0.0f, 1.0f};
	float floor = k_no_floor;
	bool touching = false;

	[[nodiscard]]
	auto poseCount() const -> int {
		if (touching || not(reach > 0.0f)) {
			return 1;
		}
		return std::max(1, static_cast<int>(std::ceil(glm::length(sweep) / (0.25f * reach))));
	}

	[[nodiscard]]
	auto contains(const glm::vec3& point, int poses) const -> bool {
		if (glm::dot(point, up) < floor) {
			return false;
		}
		if (touching) {
			return segmentBoxDistanceSquared(axis_start, axis_start + sweep, point, 0.5f * voxel::k_voxel_size) <=
			       (reach * reach) + 1.0e-8f;
		}

		const glm::vec3 axis = axis_end - axis_start;
		const float axis_squared = glm::dot(axis, axis);
		const float reach_squared = reach * reach;
		for (int pose = 0; pose <= poses; ++pose) {
			const glm::vec3 offset = point - (axis_start + (sweep * (static_cast<float>(pose) / static_cast<float>(poses))));
			const float along = axis_squared > 0.0f ? glm::clamp(glm::dot(offset, axis) / axis_squared, 0.0f, 1.0f) : 0.0f;
			const glm::vec3 from_axis = offset - (axis * along);
			if (glm::dot(from_axis, from_axis) <= reach_squared) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]]
	auto bounds() const -> std::pair<glm::vec3, glm::vec3> {
		const glm::vec3 low = glm::min(axis_start, axis_end);
		const glm::vec3 high = glm::max(axis_start, axis_end);
		return {glm::min(low, low + sweep) - glm::vec3(reach), glm::max(high, high + sweep) + glm::vec3(reach)};
	}
};

struct SmashPieces {
	std::vector<DetachedComponent> pieces;
	uint32_t voxels = 0;
	glm::vec3 index_sum {0.0f};
};

[[nodiscard]]
inline auto collectSmashPieces(
    const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const SmashVolume& smash,
    float energy
) -> SmashPieces {
	SmashPieces out;
	if (materials.materials.empty()) {
		return out;
	}

	const auto [low, high] = smash.bounds();
	const glm::ivec3 voxel_dims = glm::ivec3(volume.voxelDims());
	glm::ivec3 first = glm::ivec3(glm::floor(low / voxel::k_voxel_size));
	glm::ivec3 last = glm::ivec3(glm::ceil(high / voxel::k_voxel_size)) - 1;
	first = glm::clamp(first, glm::ivec3(0), voxel_dims - 1);
	last = glm::clamp(last, glm::ivec3(0), voxel_dims - 1);
	if (first.x > last.x || first.y > last.y || first.z > last.z) {
		return out;
	}

	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);
	const glm::ivec3 first_brick = first / brick_dim;
	const glm::ivec3 last_brick = last / brick_dim;
	const int poses = smash.poseCount();

	for (int32_t bz = first_brick.z; bz <= last_brick.z; ++bz) {
		for (int32_t by = first_brick.y; by <= last_brick.y; ++by) {
			for (int32_t bx = first_brick.x; bx <= last_brick.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				const voxel::BrickOccupancy* occupancy = volume.occupancyPointer(brick);
				if (occupancy == nullptr) {
					continue;
				}

				voxel::BrickOccupancy mask {};
				const glm::ivec3 brick_first = glm::max(first, brick * brick_dim);
				const glm::ivec3 brick_last = glm::min(last, (brick * brick_dim) + (brick_dim - 1));
				for (int32_t z = brick_first.z; z <= brick_last.z; ++z) {
					for (int32_t y = brick_first.y; y <= brick_last.y; ++y) {
						for (int32_t x = brick_first.x; x <= brick_last.x; ++x) {
							const glm::ivec3 voxel {x, y, z};
							const glm::ivec3 local = voxel - (brick * brick_dim);
							if (not voxel::isSolid(
							        *occupancy, static_cast<uint32_t>(local.x), static_cast<uint32_t>(local.y), static_cast<uint32_t>(local.z)
							    )) {
								continue;
							}

							const glm::vec3 center = (glm::vec3 {voxel} + 0.5f) * voxel::k_voxel_size;
							if (not smash.contains(center, poses)) {
								continue;
							}

							const uint8_t palette_index = volume.materialAt(voxel);
							const voxel::PhysicalMaterial& material =
							    materials.materials[voxel::resolveMaterialIndex(palette, materials, palette_index)];
							if (material.isIndestructible() || material.toughness > energy) {
								continue;
							}

							voxel::setSolid(
							    mask, static_cast<uint32_t>(local.x), static_cast<uint32_t>(local.y), static_cast<uint32_t>(local.z), true
							);
							out.index_sum += glm::vec3(voxel);
							++out.voxels;
						}
					}
				}

				if (voxel::isEmpty(mask)) {
					continue;
				}
				for (const voxel::BrickOccupancy& region : voxel::brickComponents(mask)) {
					DetachedComponent piece;
					piece.component = static_cast<uint32_t>(out.pieces.size());
					piece.voxel_count = voxel::popCount(region);
					piece.pieces.push_back(voxel::BrickPiece {.brick = brick, .voxels = region, .component = piece.component});
					out.pieces.push_back(std::move(piece));
				}
			}
		}
	}

	return out;
}

}
