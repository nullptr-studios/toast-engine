#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <toast/voxel/ray_march.hpp>
#include <utility>
#include <vector>

using namespace toast::voxel;
using namespace voxeltest;

namespace {

constexpr float k_far = std::numeric_limits<float>::max();
constexpr float k_tolerance = 4e-3f;

struct Scene {
	std::vector<const Volume*> volumes;
	Palette palette;
	gpu::PackedPool pool;
	gpu::PackedScene packed;

	Scene(const BrickPool& bricks, std::vector<const Volume*> sources) : volumes(std::move(sources)) {
		std::vector<gpu::SceneVolume> scene_volumes;
		for (const Volume* volume : volumes) {
			scene_volumes.push_back({volume, &palette});
		}
		pool = gpu::packPool(bricks);
		packed = gpu::packScene(scene_volumes);
	}

	[[nodiscard]]
	auto march(uint32_t v, glm::vec3 origin, glm::vec3 direction, float t_min = 0.0f, float t_max = k_far) const -> RayHit {
		return marchRay(pool, packed, v, origin, direction, t_min, t_max);
	}
};

struct Expected {
	bool hit = false;
	float t = 0.0f;
	int32_t axis = -1;
	bool tied = false;
};

/// Slab test against every solid voxel
[[nodiscard]]
auto bruteForce(const Volume& volume, glm::vec3 origin, glm::vec3 direction, float t_min) -> Expected {
	Expected best;
	forEachCell(glm::ivec3(volume.voxelDims()), [&](glm::ivec3 voxel) {
		if (!volume.isSolidAt(voxel)) {
			return;
		}
		float enter = -k_far;
		float leave = k_far;
		int32_t axis = -1;
		for (int32_t a = 0; a < 3; ++a) {
			const auto low = static_cast<float>(voxel[a]);
			if (direction[a] == 0.0f) {
				leave = origin[a] < low || origin[a] > low + 1.0f ? -k_far : leave;
				continue;
			}
			const float t0 = (low - origin[a]) / direction[a];
			const float t1 = (low + 1.0f - origin[a]) / direction[a];
			if (std::min(t0, t1) > enter) {
				enter = std::min(t0, t1);
				axis = a;
			}
			leave = std::min(leave, std::max(t0, t1));
		}
		if (enter < t_min) {
			enter = t_min;
			axis = -1;
		}
		if (enter > leave) {
			return;
		}
		if (!best.hit || enter < best.t - 1e-4f) {
			best = Expected {.hit = true, .t = enter, .axis = axis};
		} else if (std::abs(enter - best.t) <= 1e-4f) {
			best.tied = true;
		}
	});
	return best;
}

}

TOAST_TEST_NAMED("voxel", "voxel/21-ray-march", test_voxel_21_ray_march) {
	Rng rng {0x2100'0000'0000'0021ull};
	BrickPool bricks(32);

	Volume a(bricks, glm::uvec3(3, 2, 2));
	fillRandom(a, glm::ivec3(0), rng, 25);
	fillRandom(a, glm::ivec3(2, 1, 1), rng, 10);
	fillRandom(a, glm::ivec3(1, 0, 1), rng, 3);
	a.setBrickUniform(glm::ivec3(1, 1, 0), 42);

	Volume b(bricks, glm::uvec3(9, 1, 5));
	b.setVoxel(glm::ivec3(4), 7);
	b.setVoxel(glm::ivec3(70, 3, 36), 9);
	b.setVoxel(glm::ivec3(40, 5, 20), 11);

	const Scene scene(bricks, {&a, &b});
	uint32_t hits = 0;

	const auto check = [&](const Scene& in, uint32_t v, glm::vec3 origin, glm::vec3 direction) {
		const Volume& volume = *in.volumes[v];
		const RayHit hit = in.march(v, origin, direction);
		const Expected expected = bruteForce(volume, origin, direction, 0.0f);
		assert(hit.hit == expected.hit);
		if (!hit.hit) {
			return;
		}
		++hits;
		const float scale = glm::length(direction);
		assert(std::abs(hit.t - expected.t) * scale <= k_tolerance);
		assert(volume.isSolidAt(hit.voxel) && hit.material == volume.materialAt(hit.voxel));

		if (hit.axis == -1) {
			assert(expected.axis == -1 || expected.t * scale <= k_tolerance);
		} else if (hit.axis != expected.axis && !expected.tied) {
			const float plane = static_cast<float>(hit.voxel[hit.axis] + (direction[hit.axis] > 0.0f ? 0 : 1));
			assert(std::abs((plane - origin[hit.axis]) / direction[hit.axis] - expected.t) * scale <= k_tolerance);
		}
	};

	for (uint32_t v = 0; v < 2; ++v) {
		const glm::vec3 dims(scene.volumes[v]->voxelDims());
		const auto inside = [&rng, dims](float margin) {
			return glm::vec3(rng.uniform(-margin, dims.x + margin), rng.uniform(-margin, dims.y + margin), rng.uniform(-margin, dims.z + margin));
		};

		for (int i = 0; i < 1500; ++i) {
			const glm::vec3 origin = inside(20.0f);
			check(scene, v, origin, inside(0.0f) - origin);
			check(scene, v, inside(-0.01f), glm::vec3(rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)));
		}

		for (int i = 0; i < 300; ++i) {
			for (int32_t axis = 0; axis < 3; ++axis) {
				glm::vec3 origin = glm::floor(inside(0.0f)) + 0.37f;
				glm::vec3 direction(0.0f);
				origin[axis] = -5.0f;
				direction[axis] = 1.0f;
				check(scene, v, origin, direction);
				origin[axis] = dims[axis] + 5.0f;
				direction[axis] = -1.0f;
				check(scene, v, origin, direction);
			}
		}

		assert(!scene.march(v, glm::vec3(-10.0f), glm::vec3(-1.0f, 0.0f, 0.0f)).hit);
		assert(!scene.march(v, glm::vec3(-10.0f, -10.0f, 2.0f), glm::vec3(0.0f, 0.0f, 1.0f)).hit);
	}
	assert(hits > 1000);

	{
		const RayHit started_inside = scene.march(0, glm::vec3(12.5f, 12.5f, 4.5f), glm::vec3(1.0f, 0.3f, 0.2f));
		assert(started_inside.hit && started_inside.t == 0.0f && started_inside.axis == -1 && started_inside.material == 42);

		const glm::vec3 origin(0.5f, 4.5f, 4.5f);
		const glm::vec3 along_x(1.0f, 0.0f, 0.0f);
		const RayHit hit = scene.march(1, origin, along_x);
		assert(hit.hit && hit.voxel == glm::ivec3(4) && hit.axis == 0 && std::abs(hit.t - 3.5f) < 1e-3f);

		const RayHit from_t_min = scene.march(1, origin, along_x, 4.0f);
		assert(from_t_min.hit && from_t_min.axis == -1 && from_t_min.t == 4.0f);
		assert(!scene.march(1, origin, along_x, 5.0f, 8.0f).hit);

		const RayHit skipped = scene.march(1, glm::vec3(8.5f, 3.5f, 36.5f), along_x);
		assert(skipped.hit && skipped.voxel == glm::ivec3(70, 3, 36) && skipped.steps < 20);
		const RayHit starved = marchRay(scene.pool, scene.packed, 1, glm::vec3(8.5f, 3.5f, 36.5f), along_x, 0.0f, k_far, 2);
		assert(!starved.hit && starved.steps == 2);
	}

	{
		BrickPool wall_bricks(8);
		Volume wall(wall_bricks, glm::uvec3(2, 1, 1));
		for (int32_t x = 0; x < 16; ++x) {
			wall.setVoxel(glm::ivec3(x, 2, 4), 5);
		}
		const Scene graze(wall_bricks, {&wall});

		const RayHit under = graze.march(0, glm::vec3(-2.0f, 2.999f, 4.5f), glm::vec3(1.0f, 0.0001f, 0.0f));
		assert(under.hit && under.voxel == glm::ivec3(0, 2, 4) && under.axis == 0 && std::abs(under.t - 2.0f) < 1e-3f);
		const RayHit over = graze.march(0, glm::vec3(-2.0f, 2.0005f, 4.5f), glm::vec3(1.0f, -0.0001f, 0.0f));
		assert(over.hit && over.voxel == glm::ivec3(0, 2, 4) && over.axis == 0);
		assert(graze.march(0, glm::vec3(-2.0f, 2.5f, 4.9995f), glm::vec3(1.0f, 0.0f, 0.00005f)).voxel == glm::ivec3(0, 2, 4));
		const RayHit landing = graze.march(0, glm::vec3(0.5f, 3.0005f, 4.5f), glm::vec3(1.0f, -0.0001f, 0.0f));
		assert(landing.hit && landing.axis == 1 && landing.voxel.y == 2 && std::abs(landing.t - 5.0f) < 1e-2f);

		hits = 0;
		const std::array<std::array<float, 2>, 3> boundaries {{{0.0f, 16.0f}, {2.0f, 3.0f}, {4.0f, 5.0f}}};
		for (int i = 0; i < 3000; ++i) {
			const uint32_t major = rng.below(3);
			glm::vec3 origin(0.0f);
			glm::vec3 direction(0.0f);
			for (uint32_t a = 0; a < 3; ++a) {
				if (a == major) {
					direction[a] = rng.chance(50) ? 1.0f : -1.0f;
					origin[a] = direction[a] > 0.0f ? -3.0f : 19.0f;
				} else {
					const float boundary = a == 0 ? static_cast<float>(rng.below(17)) : boundaries[a][rng.below(2)];
					direction[a] = rng.uniform(-0.002f, 0.002f);
					origin[a] = boundary + rng.uniform(-0.001f, 0.001f);
				}
			}
			check(graze, 0, origin, direction);
		}
		assert(hits > 50);
	}
}
