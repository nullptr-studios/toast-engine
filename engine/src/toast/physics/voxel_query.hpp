/**
 * @file voxel_query.hpp
 * @author Xein
 * @date 17 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include <algorithm>
#include <glm/glm.hpp>
#include <span>
#include <toast/physics/aabb.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <type_traits>

namespace physics {

struct VoxelQueryContext {
	const voxel::Volume& volume;
	const voxel::VolumeSurface& surface;
	const voxel::Palette& palette;
	const voxel::MaterialLibrary& materials;
};

struct VoxelCandidate {
	glm::ivec3 voxel;
	glm::ivec3 brick;
	uint32_t brick_slot;
	uint16_t local_index;
	glm::vec3 min;
	glm::vec3 max;
	uint8_t normal_index;
	voxel::VoxelClass classification;
	uint8_t palette_index;
	const voxel::PhysicalMaterial* material;
};

namespace _detail {

template<typename Callback>
[[nodiscard]]
auto invokeVoxelCallback(Callback& callback, const VoxelCandidate& candidate) -> bool {
	if constexpr (std::is_same_v<std::invoke_result_t<Callback&, const VoxelCandidate&>, bool>) {
		return callback(candidate);
	} else {
		callback(candidate);
		return true;
	}
}

}

template<typename Callback>
void queryVoxelSurface(const VoxelQueryContext& context, const AABB& aabb, Callback&& callback) {
	// floor and ceil before clamping or a negative corner loses the info needed to reject it
	glm::ivec3 first = glm::ivec3(glm::floor(aabb.min / voxel::k_voxel_size));
	glm::ivec3 last = glm::ivec3(glm::ceil(aabb.max / voxel::k_voxel_size)) - 1;

	const glm::ivec3 voxel_dims = glm::ivec3(context.volume.voxelDims());
	first = glm::clamp(first, glm::ivec3(0), voxel_dims - 1);
	last = glm::clamp(last, glm::ivec3(0), voxel_dims - 1);
	if (first.x > last.x || first.y > last.y || first.z > last.z) {
		return;
	}

	const glm::ivec3 first_brick = first >> 3;
	const glm::ivec3 last_brick = last >> 3;
	const glm::ivec3 brick_dims = glm::ivec3(context.volume.brickDims());
	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);

	bool stopped = false;

	const auto processBrick = [&](glm::ivec3 brick) {
		const voxel::BrickEntry entry = context.volume.entryAt(brick);
		if (entry.tag() == voxel::BrickTag::empty) {
			return;
		}

		const std::span<const voxel::SurfaceVoxel> surface = context.surface.brickSurface(brick);
		if (surface.empty()) {
			return;
		}

		// private in Volume and VolumeSurface so recompute it the same way here
		const uint32_t brick_slot = static_cast<uint32_t>(brick.x) + static_cast<uint32_t>(brick.y) * brick_dims.x +
		                            static_cast<uint32_t>(brick.z) * brick_dims.x * brick_dims.y;

		const glm::ivec3 brick_base = brick * brick_dim;
		const auto emit = [&](const voxel::SurfaceVoxel& surface_voxel, glm::ivec3 global_voxel) -> bool {
			const uint8_t palette_index = context.volume.materialAt(global_voxel);
			const uint32_t material_index = voxel::resolveMaterialIndex(context.palette, context.materials, palette_index);
			const voxel::PhysicalMaterial& material = context.materials.materials[material_index];
			if (not material.collides()) {
				return true;
			}

			const glm::vec3 min = glm::vec3(global_voxel) * voxel::k_voxel_size;
			const glm::vec3 max = min + glm::vec3(voxel::k_voxel_size);

			return _detail::invokeVoxelCallback(
			    callback,
			    VoxelCandidate {
			      .voxel = global_voxel,
			      .brick = brick,
			      .brick_slot = brick_slot,
			      .local_index = surface_voxel.local_index,
			      .min = min,
			      .max = max,
			      .normal_index = voxel::normalOf(surface_voxel.classification),
			      .classification = voxel::classOf(surface_voxel.classification),
			      .palette_index = palette_index,
			      .material = &material,
			    }
			);
		};

		const glm::ivec3 clipped_min = glm::max(first, brick_base);
		const glm::ivec3 clipped_max = glm::min(last, brick_base + (brick_dim - 1));
		const glm::ivec3 clipped_dims = clipped_max - clipped_min + 1;
		const auto clipped_voxels =
		    static_cast<size_t>(clipped_dims.x) * static_cast<size_t>(clipped_dims.y) * static_cast<size_t>(clipped_dims.z);

		if (clipped_voxels >= surface.size()) {
			for (const voxel::SurfaceVoxel& surface_voxel : surface) {
				const voxel::BrickCoord local = voxel::localFromIndex(surface_voxel.local_index);
				const glm::ivec3 global_voxel =
				    brick_base + glm::ivec3 {static_cast<int32_t>(local.x), static_cast<int32_t>(local.y), static_cast<int32_t>(local.z)};

				if (glm::any(glm::lessThan(global_voxel, first)) || glm::any(glm::greaterThan(global_voxel, last))) {
					continue;
				}
				if (not emit(surface_voxel, global_voxel)) {
					stopped = true;
					return;
				}
			}
			return;
		}

		for (int32_t vz = clipped_min.z; vz <= clipped_max.z; ++vz) {
			for (int32_t vy = clipped_min.y; vy <= clipped_max.y; ++vy) {
				for (int32_t vx = clipped_min.x; vx <= clipped_max.x; ++vx) {
					const auto local_index = static_cast<uint16_t>(
					    (static_cast<uint32_t>(vz - brick_base.z) << 6u) | (static_cast<uint32_t>(vy - brick_base.y) << 3u) |
					    static_cast<uint32_t>(vx - brick_base.x)
					);

					const auto found = std::lower_bound(
					    surface.begin(), surface.end(), local_index, [](const voxel::SurfaceVoxel& candidate, uint16_t index) {
						    return candidate.local_index < index;
					    }
					);
					if (found == surface.end() || found->local_index != local_index) {
						continue;
					}
					if (not emit(*found, glm::ivec3 {vx, vy, vz})) {
						stopped = true;
						return;
					}
				}
			}
		}
	};

	const uint64_t box_bricks = static_cast<uint64_t>(last_brick.x - first_brick.x + 1) *
	                            static_cast<uint64_t>(last_brick.y - first_brick.y + 1) *
	                            static_cast<uint64_t>(last_brick.z - first_brick.z + 1);
	const uint64_t populated_bricks = static_cast<uint64_t>(context.surface.populatedBrickCount());

	if (populated_bricks < box_bricks) {
		context.surface.forEachPopulatedBrick([&](glm::ivec3 brick) {
			if (stopped) {
				return;
			}
			if (glm::any(glm::lessThan(brick, first_brick)) || glm::any(glm::greaterThan(brick, last_brick))) {
				return;
			}
			processBrick(brick);
		});
	} else {
		for (int32_t z = first_brick.z; z <= last_brick.z && not stopped; ++z) {
			for (int32_t y = first_brick.y; y <= last_brick.y && not stopped; ++y) {
				for (int32_t x = first_brick.x; x <= last_brick.x && not stopped; ++x) {
					processBrick({x, y, z});
				}
			}
		}
	}
}

}
