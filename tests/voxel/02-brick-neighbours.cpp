#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <array>
#include <cassert>

using namespace voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/02-brick-neighbours", test_voxel_02_brick_neighbours) {
	using Gather = BrickOccupancy (*)(const BrickOccupancy&, const BrickOccupancy*) noexcept;
	const std::array<Gather, 6> gather {&neighboursNegX, &neighboursPosX, &neighboursNegY, &neighboursPosY, &neighboursNegZ, &neighboursPosZ};

	Rng rng {0x0BAD'C0DE'0BAD'C0DEull};
	for (uint32_t i = 0; i < 128; ++i) {
		const uint32_t percent = 5 + i % 10 * 10;
		const BrickOccupancy brick = randomBrick(rng, percent);
		const RandomSides sides(rng, percent, i % 64);
		const BrickNeighbourhood around = sides.view();

		for (uint32_t face = 0; face < 6; ++face) {
			const BrickOccupancy gathered = gather[face](brick, sides.side(face));
			forEachCell(glm::ivec3(8), [&](glm::ivec3 v) {
				assert(isSolidAt(gathered, v) == solidAcross(brick, around, v + k_steps[face]));
			});
		}

		const BrickOccupancy inner = interior(brick, around);
		const BrickOccupancy grown = dilate(brick);
		forEachCell(glm::ivec3(8), [&](glm::ivec3 v) {
			bool enclosed = isSolidAt(brick, v);
			bool touched = isSolidAt(brick, v);
			for (const glm::ivec3& step : k_steps) {
				enclosed = enclosed && solidAcross(brick, around, v + step);
				touched = touched || solidAcross(brick, BrickNeighbourhood {}, v + step);
			}
			assert(isSolidAt(inner, v) == enclosed);
			assert(isSolidAt(grown, v) == touched);
		});
		assert(surfaceShell(brick, around) == (brick & ~inner));
	}
}
