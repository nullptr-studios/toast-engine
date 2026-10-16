/**
 * @file voxel_query.hpp
 * @author Xein
 * @date 17 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
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
	uint8_t exposure = voxel::k_all_faces_exposed;
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
	if (glm::any(glm::lessThan(last, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(first, voxel_dims))) {
		return;
	}
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

	const auto process_brick = [&](glm::ivec3 brick) {
		const voxel::BrickEntry entry = context.volume.entryAt(brick);
		if (entry.tag() == voxel::BrickTag::empty) {
			return;
		}

		const std::span<const voxel::SurfaceVoxel> surface = context.surface.brickSurface(brick);
		if (surface.empty()) {
			return;
		}

		// private in Volume and VolumeSurface so recompute it the same way here
		const uint32_t brick_slot = static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * brick_dims.x) +
		                            (static_cast<uint32_t>(brick.z) * brick_dims.x * brick_dims.y);

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
			      .exposure = surface_voxel.exposure,
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
			process_brick(brick);
		});
	} else {
		for (int32_t z = first_brick.z; z <= last_brick.z && not stopped; ++z) {
			for (int32_t y = first_brick.y; y <= last_brick.y && not stopped; ++y) {
				for (int32_t x = first_brick.x; x <= last_brick.x && not stopped; ++x) {
					process_brick({x, y, z});
				}
			}
		}
	}
}

struct VoxelRayEntry {
	float distance = 0.0f;
	glm::vec3 normal = {};
	glm::ivec3 voxel = {};
	uint8_t palette_index = voxel::k_empty_palette_index;
};

namespace _detail {

inline constexpr float k_ray_tie_epsilon = 1.0e-6f;

[[nodiscard]]
inline auto rayTieSlack(float t) -> float {
	return k_ray_tie_epsilon * std::max(1.0f, std::abs(t));
}

}

/**
 * @param bounds Where the voxels are
 * @param hit_passable whether voxels of passable materials count as solid
 * @param on_entry is given each @c VoxelRayEntry and returns how far the walk may go on in metres
 */
template<typename OnEntry>
void queryVoxelRay(
    const VoxelQueryContext& context, const AABB& bounds, glm::vec3 origin, glm::vec3 direction, float max_distance,
    bool hit_passable, OnEntry&& on_entry
) {
	constexpr float k_infinity = std::numeric_limits<float>::infinity();
	constexpr auto k_brick = static_cast<int32_t>(voxel::k_brick_dim);
	constexpr float k_voxel = voxel::k_voxel_size;

	const voxel::Volume& volume = context.volume;
	const glm::ivec3 dims = glm::ivec3(volume.voxelDims());

	// from here everything is in voxel size
	const glm::vec3 start = origin / k_voxel;
	const glm::vec3 box_min = glm::max(glm::floor(bounds.min / k_voxel - 1.0e-3f), glm::vec3(0.0f));
	const glm::vec3 box_max = glm::min(glm::ceil(bounds.max / k_voxel + 1.0e-3f), glm::vec3(dims));
	float limit = max_distance / k_voxel;

	std::array<float, 3> entry_times {-k_infinity, -k_infinity, -k_infinity};
	glm::vec3 inverse(0.0f);
	glm::vec3 sign(0.0f);
	float t_exit = k_infinity;

	for (int axis = 0; axis < 3; ++axis) {
		if (direction[axis] == 0.0f) {
			if (start[axis] < box_min[axis] || start[axis] > box_max[axis]) {
				return;
			}
			continue;
		}

		inverse[axis] = 1.0f / direction[axis];
		sign[axis] = direction[axis] > 0.0f ? 1.0f : -1.0f;
		const float t0 = (box_min[axis] - start[axis]) * inverse[axis];
		const float t1 = (box_max[axis] - start[axis]) * inverse[axis];
		entry_times[axis] = std::min(t0, t1);
		t_exit = std::min(t_exit, std::max(t0, t1));
	}

	// the face it enters the box through
	float t = -k_infinity;
	int axis = -1;
	for (int candidate = 0; candidate < 3; ++candidate) {
		if (entry_times[candidate] > t) {
			t = entry_times[candidate];
			axis = candidate;
		}
	}

	// axes the ray crosses a plane of at t
	uint32_t crossed = 0;
	if (t < 0.0f) {
		t = 0.0f;
		axis = -1;
	} else {
		for (int candidate = 0; candidate < 3; ++candidate) {
			if (sign[candidate] != 0.0f && entry_times[candidate] >= t - _detail::rayTieSlack(t)) {
				crossed |= 1u << candidate;
			}
		}
	}

	// moves t to where the ray leaves the cell
	const auto leave = [&](glm::ivec3 cell_min, int32_t size) -> bool {
		std::array<float, 3> plane_times {k_infinity, k_infinity, k_infinity};
		float next = k_infinity;
		int next_axis = axis;
		for (int candidate = 0; candidate < 3; ++candidate) {
			if (sign[candidate] == 0.0f) {
				continue;
			}
			const float plane = static_cast<float>(sign[candidate] > 0.0f ? cell_min[candidate] + size : cell_min[candidate]);
			plane_times[candidate] = (plane - start[candidate]) * inverse[candidate];
			if (plane_times[candidate] < next) {
				next = plane_times[candidate];
				next_axis = candidate;
			}
		}
		if (!std::isfinite(next)) {
			return false;
		}

		crossed = 0;
		for (int candidate = 0; candidate < 3; ++candidate) {
			if (plane_times[candidate] <= next + _detail::rayTieSlack(next)) {
				crossed |= 1u << candidate;
			}
		}
		t = std::max(next, t);
		axis = next_axis;
		return true;
	};

	const auto collides = [&](uint8_t palette_index) {
		const uint32_t material = voxel::resolveMaterialIndex(context.palette, context.materials, palette_index);
		return context.materials.materials[material].collides();
	};

	// whatever happens a ray cannot cross more cells than these
	const int32_t max_steps = (3 * (dims.x + dims.y + dims.z)) + 16;
	bool in_solid = false;

	for (int32_t step = 0; step < max_steps; ++step) {
		if (t > std::min(t_exit, limit)) {
			return;
		}

		const glm::vec3 position = start + (direction * t);
		glm::ivec3 cell;
		for (int candidate = 0; candidate < 3; ++candidate) {
			if ((crossed & (1u << candidate)) != 0) {
				const auto plane = static_cast<int32_t>(std::lround(position[candidate]));
				cell[candidate] = sign[candidate] > 0.0f ? plane : plane - 1;
			} else if (sign[candidate] < 0.0f) {
				// a ray that is going down is in the cell below it
				cell[candidate] = static_cast<int32_t>(std::ceil(position[candidate])) - 1;
			} else {
				cell[candidate] = static_cast<int32_t>(std::floor(position[candidate]));
			}
		}
		if (glm::any(glm::lessThan(cell, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(cell, dims))) {
			return;
		}

		const glm::ivec3 brick = cell >> 3;
		const glm::ivec3 brick_min = brick * k_brick;
		const voxel::BrickEntry entry = volume.entryAt(brick);

		if (entry.tag() == voxel::BrickTag::empty) {
			in_solid = false;
			if (!leave(brick_min, k_brick)) {
				return;
			}
			continue;
		}

		// A uniform brick is one material all through
		const bool uniform = entry.tag() == voxel::BrickTag::uniform;
		uint8_t palette_index = voxel::k_empty_palette_index;
		if (uniform) {
			palette_index = static_cast<uint8_t>(entry.payload());
		} else {
			const voxel::BrickOccupancy* occupancy = volume.occupancyPointer(brick);
			const glm::uvec3 local = glm::uvec3(cell - brick_min);
			if (occupancy != nullptr && voxel::isSolid(*occupancy, local.x, local.y, local.z)) {
				palette_index = volume.materialAt(cell);
			}
		}

		const bool solid = palette_index != voxel::k_empty_palette_index && (hit_passable || collides(palette_index));
		if (solid && !in_solid) {
			glm::vec3 normal = -direction;
			if (axis >= 0) {
				normal = glm::vec3(0.0f);
				normal[axis] = -sign[axis];
			}
			const float reach =
			    on_entry(VoxelRayEntry {.distance = t * k_voxel, .normal = normal, .voxel = cell, .palette_index = palette_index});
			limit = std::min(limit, reach / k_voxel);
		}
		in_solid = solid;

		if (!(uniform ? leave(brick_min, k_brick) : leave(cell, 1))) {
			return;
		}
	}
}

}
