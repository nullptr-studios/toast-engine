/**
 * @file voxel_query.hpp
 * @author Xein
 * @date 17 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include <glm/glm.hpp>
#include <toast/physics/aabb.hpp>
#include <toast/voxel/palette.hpp>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>

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

	for (int32_t z = first_brick.z; z <= last_brick.z; ++z) {
		for (int32_t y = first_brick.y; y <= last_brick.y; ++y) {
			for (int32_t x = first_brick.x; x <= last_brick.x; ++x) {
				const glm::ivec3 brick {x, y, z};

				const voxel::BrickEntry entry = context.volume.entryAt(brick);
				if (entry.tag() == voxel::BrickTag::empty) {
					continue;
				}

				// private in Volume and VolumeSurface so recompute it the same way here
				const uint32_t brick_slot = static_cast<uint32_t>(brick.x) + static_cast<uint32_t>(brick.y) * brick_dims.x +
				                            static_cast<uint32_t>(brick.z) * brick_dims.x * brick_dims.y;

				for (const voxel::SurfaceVoxel& sv : context.surface.brickSurface(brick)) {
					const voxel::BrickCoord local = voxel::localFromIndex(sv.local_index);
					const glm::ivec3 local_offset {
					  static_cast<int32_t>(local.x), static_cast<int32_t>(local.y), static_cast<int32_t>(local.z)
					};
					const glm::ivec3 global_voxel = brick * static_cast<int32_t>(voxel::k_brick_dim) + local_offset;

					// the brick overlaps the query but this exposed voxel inside it might not
					if (glm::any(glm::lessThan(global_voxel, first)) || glm::any(glm::greaterThan(global_voxel, last))) {
						continue;
					}

					const uint8_t palette_index = context.volume.materialAt(global_voxel);
					const uint32_t material_index = voxel::resolveMaterialIndex(context.palette, context.materials, palette_index);
					const voxel::PhysicalMaterial& material = context.materials.materials[material_index];
					if (not material.collides()) {
						continue;
					}

					const glm::vec3 min = glm::vec3(global_voxel) * voxel::k_voxel_size;
					const glm::vec3 max = min + glm::vec3(voxel::k_voxel_size);

					callback(
					    VoxelCandidate {
					      .voxel = global_voxel,
					      .brick = brick,
					      .brick_slot = brick_slot,
					      .local_index = sv.local_index,
					      .min = min,
					      .max = max,
					      .normal_index = voxel::normalOf(sv.classification),
					      .classification = voxel::classOf(sv.classification),
					      .palette_index = palette_index,
					      .material = &material,
					    }
					);
				}
			}
		}
	}
}

}
