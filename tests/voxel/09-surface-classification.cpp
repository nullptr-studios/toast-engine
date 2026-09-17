#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>
#include <toast/voxel/surface.hpp>
#include <vector>

using namespace toast::voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/09-surface-classification", test_voxel_09_surface_classification) {
	for (uint8_t index = 0; index < k_normal_direction_count; ++index) {
		const glm::ivec3 direction = normalDirection(index);
		assert(direction != glm::ivec3(0) && glm::all(glm::lessThanEqual(glm::abs(direction), glm::ivec3(1))));
		assert(normalIndexOf(direction) == index);
	}
	assert(normalDirection(k_normal_undefined) == glm::ivec3(0) && normalIndexOf(glm::ivec3(0)) == k_normal_undefined);

	for (uint8_t type = 0; type <= static_cast<uint8_t>(VoxelClass::corner); ++type) {
		for (uint8_t normal = 0; normal <= k_normal_undefined; ++normal) {
			const uint8_t packed = packClassification(static_cast<VoxelClass>(type), normal);
			assert(classOf(packed) == static_cast<VoxelClass>(type) && normalOf(packed) == normal);
		}
	}

	{
		const std::vector<SurfaceVoxel> surface = buildBrickSurface(k_full_brick, BrickNeighbourhood {});
		const auto classAt = [&surface](uint32_t x, uint32_t y, uint32_t z) -> uint8_t {
			for (const SurfaceVoxel& entry : surface) {
				if (entry.local_index == localIndex(x, y, z)) {
					return entry.classification;
				}
			}
			return 0xFF;
		};
		assert(surface.size() == k_brick_voxel_count - 6 * 6 * 6);
		assert(classAt(3, 3, 0) == packClassification(VoxelClass::face, normalIndexOf(glm::ivec3(0, 0, -1))));
		assert(classAt(3, 0, 0) == packClassification(VoxelClass::edge, normalIndexOf(glm::ivec3(0, -1, -1))));
		assert(classAt(7, 7, 7) == packClassification(VoxelClass::corner, normalIndexOf(glm::ivec3(1))));
		assert(classAt(3, 3, 3) == 0xFF);

		const BrickNeighbourhood enclosed {&k_full_brick, &k_full_brick, &k_full_brick, &k_full_brick, &k_full_brick, &k_full_brick};
		assert(buildBrickSurface(k_full_brick, enclosed).empty());

		BrickOccupancy wall {};
		wall[3] = ~0ull;
		const std::vector<SurfaceVoxel> thin = buildBrickSurface(wall, BrickNeighbourhood {&k_full_brick, &k_full_brick, &k_full_brick, &k_full_brick});
		assert(thin.size() == 64);
		for (const SurfaceVoxel& entry : thin) {
			assert(entry.classification == packClassification(VoxelClass::corner, k_normal_undefined));
		}
	}

	Rng rng {0x5A5A'1234'A5A5'4321ull};
	for (uint32_t i = 0; i < 128; ++i) {
		const uint32_t percent = 5 + i % 10 * 10;
		const BrickOccupancy brick = randomBrick(rng, percent);
		const RandomSides sides(rng, percent, i % 64);
		const BrickNeighbourhood around = sides.view();

		const auto solid = [&](glm::ivec3 v) { return solidAcross(brick, around, v); };
		assert(buildBrickSurface(brick, around) == referenceSurface(solid, glm::ivec3(0)));
	}
}
