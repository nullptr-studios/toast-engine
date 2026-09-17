#include "test_registry.hpp"

#include <algorithm>
#include <cassert>
#include <toast/voxel/brick_pool.hpp>

using namespace toast::voxel;

TOAST_TEST_NAMED("voxel", "voxel/06-brick-pool", test_voxel_06_brick_pool) {
	BrickPool pool(4);
	assert(pool.capacity() == 4 && pool.freeCount() == 4);

	for (uint32_t expected = 0; expected < 4; ++expected) {
		assert(pool.allocate() == expected);
	}
	assert(pool.allocate() == k_invalid_brick);
	assert(pool.allocatedCount() == 4 && pool.freeCount() == 0);

	for (uint32_t id = 0; id < 4; ++id) {
		std::ranges::fill(pool.material(id), static_cast<uint8_t>(id + 1));
		setSolid(pool.occupancy(id), id, true);
	}
	for (uint32_t id = 0; id < 4; ++id) {
		assert(std::ranges::all_of(pool.material(id), [id](uint8_t value) { return value == id + 1; }));
		assert(popCount(pool.occupancy(id)) == 1 && isSolid(pool.occupancy(id), id));
	}

	pool.free(2);
	assert(pool.allocatedCount() == 3);
	assert(pool.allocate() == 2);
	assert(isEmpty(pool.occupancy(2)));
	assert(std::ranges::all_of(pool.material(2), [](uint8_t value) { return value == k_empty_palette_index; }));
}
