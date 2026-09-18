#include "ray_march.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace voxel {

namespace {

[[nodiscard]]
auto marchTieSlack(float t) -> float {
	return k_march_tie_epsilon * std::max(1.0f, std::abs(t));
}

}

auto marchRay(
    const gpu::PackedPool& pool, const gpu::PackedScene& scene, uint32_t volume, glm::vec3 origin, glm::vec3 direction,
    float t_min, float t_max, uint32_t max_steps
) -> RayHit {
	constexpr float k_infinity = std::numeric_limits<float>::infinity();
	constexpr int32_t k_coarse_voxels = static_cast<int32_t>(gpu::k_coarse_bricks * k_brick_dim);
	constexpr auto k_brick = static_cast<int32_t>(k_brick_dim);

	RayHit out;

	const gpu::VolumeRecord& record = scene.records[volume];
	const glm::uvec3 brick_dims(record.brick_dims_x, record.brick_dims_y, record.brick_dims_z);
	const glm::ivec3 dims = glm::ivec3(brick_dims) * k_brick;

	float t_exit = k_infinity;
	std::array<float, 3> entry_times {-k_infinity, -k_infinity, -k_infinity};
	glm::vec3 inverse(0.0f);
	glm::vec3 step_sign(0.0f);

	for (int32_t a = 0; a < 3; ++a) {
		if (direction[a] == 0.0f) {
			// Parallel to the slabs of this axis so inside them for the whole ray or never
			if (origin[a] < 0.0f || origin[a] > static_cast<float>(dims[a])) {
				return out;
			}
			continue;
		}

		inverse[a] = 1.0f / direction[a];
		step_sign[a] = direction[a] > 0.0f ? 1.0f : -1.0f;

		const float t0 = (0.0f - origin[a]) * inverse[a];
		const float t1 = (static_cast<float>(dims[a]) - origin[a]) * inverse[a];
		entry_times[a] = std::min(t0, t1);
		t_exit = std::min(t_exit, std::max(t0, t1));
	}

	float t = -k_infinity;
	int32_t axis = -1;
	for (int32_t a = 0; a < 3; ++a) {
		if (entry_times[a] > t) {
			t = entry_times[a];
			axis = a;
		}
	}

	// Crossed axes take the index from the plane since floor(position) can land on either side of it
	uint32_t crossed = 0;
	if (t < t_min) {
		t = t_min;
		axis = -1;
	} else {
		for (int32_t a = 0; a < 3; ++a) {
			if (entry_times[a] >= t - marchTieSlack(t)) {
				crossed |= 1u << a;
			}
		}
	}

	t_exit = std::min(t_exit, t_max);
	if (t > t_exit) {
		return out;
	}

	const auto leave = [&](glm::ivec3 cell_min, int32_t size) {
		std::array<float, 3> plane_times {k_infinity, k_infinity, k_infinity};
		float next = k_infinity;
		int32_t next_axis = axis;
		for (int32_t a = 0; a < 3; ++a) {
			if (step_sign[a] == 0.0f) {
				continue;
			}
			const float plane = static_cast<float>(step_sign[a] > 0.0f ? cell_min[a] + size : cell_min[a]);
			plane_times[a] = (plane - origin[a]) * inverse[a];
			if (plane_times[a] < next) {
				next = plane_times[a];
				next_axis = a;
			}
		}

		crossed = 0;
		for (int32_t a = 0; a < 3; ++a) {
			if (plane_times[a] <= next + marchTieSlack(next)) {
				crossed |= 1u << a;
			}
		}
		t = std::max(next, t);
		axis = next_axis;
	};

	for (uint32_t step = 0; step < max_steps; ++step) {
		out.steps = step + 1;
		if (t > t_exit) {
			return out;
		}

		const glm::vec3 position = origin + direction * t;
		glm::ivec3 voxel = glm::ivec3(glm::floor(position));
		for (int32_t a = 0; a < 3; ++a) {
			if ((crossed & (1u << a)) != 0) {
				const auto plane = static_cast<int32_t>(std::lround(position[a]));
				voxel[a] = step_sign[a] > 0.0f ? plane : plane - 1;
			}
		}
		if (glm::any(glm::lessThan(voxel, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxel, dims))) {
			return out;
		}

		const glm::ivec3 coarse = voxel / k_coarse_voxels;
		if (!gpu::sampleCoarse(scene, volume, coarse)) {
			leave(coarse * k_coarse_voxels, k_coarse_voxels);
			continue;
		}

		const glm::ivec3 brick = voxel / k_brick;
		const uint32_t entry = scene.grids[record.grid_offset + gpu::gridIndex(brick_dims, glm::uvec3(brick))];
		const uint32_t tag = entry & 3u;

		if (tag == static_cast<uint32_t>(BrickTag::empty)) {
			leave(brick * k_brick, k_brick);
			continue;
		}

		if (tag == static_cast<uint32_t>(BrickTag::uniform)) {
			out.hit = true;
			out.t = t;
			out.voxel = voxel;
			out.axis = axis;
			out.material = static_cast<uint8_t>(entry >> k_brick_tag_bits);
			return out;
		}

		if (gpu::sampleSolid(pool, scene, volume, voxel)) {
			out.hit = true;
			out.t = t;
			out.voxel = voxel;
			out.axis = axis;
			out.material = gpu::sampleMaterial(pool, scene, volume, voxel);
			return out;
		}

		leave(voxel, 1);
	}

	return out;
}

}
