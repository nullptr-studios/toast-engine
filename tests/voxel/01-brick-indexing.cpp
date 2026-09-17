#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>

using namespace voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/01-brick-indexing", test_voxel_01_brick_indexing) {
	assert(localIndex(1, 0, 0) == 1 && localIndex(0, 1, 0) == 8 && localIndex(0, 0, 1) == 64);

	for (uint32_t index = 0; index < k_brick_voxel_count; ++index) {
		const BrickCoord c = localFromIndex(index);
		assert(c.x < k_brick_dim && c.y < k_brick_dim && c.z < k_brick_dim);
		assert(localIndex(c.x, c.y, c.z) == index);

		BrickOccupancy one {};
		setSolid(one, c.x, c.y, c.z, true);
		assert(isSolid(one, index) && popCount(one) == 1);
		assert(one[c.z] == 1ull << (c.y * 8 + c.x));

		BrickOccupancy all_but_one = k_full_brick;
		setSolid(all_but_one, index, false);
		assert(!isSolid(all_but_one, c.x, c.y, c.z) && popCount(all_but_one) == k_brick_voxel_count - 1);
		assert(!isEmpty(all_but_one) && !isFull(all_but_one));
	}

	assert(isEmpty(k_empty_brick) && isFull(k_full_brick));

	Rng rng {0x1234'5678'9ABC'DEF0ull};
	for (uint32_t i = 0; i < 64; ++i) {
		const BrickOccupancy a = randomBrick(rng, 40);
		const BrickOccupancy b = randomBrick(rng, 40);
		const BrickOccupancy both = a & b;
		const BrickOccupancy either = a | b;
		const BrickOccupancy inverted = ~a;
		for (uint32_t index = 0; index < k_brick_voxel_count; ++index) {
			assert(isSolid(both, index) == (isSolid(a, index) && isSolid(b, index)));
			assert(isSolid(either, index) == (isSolid(a, index) || isSolid(b, index)));
			assert(isSolid(inverted, index) == !isSolid(a, index));
		}
	}
}
