#include "test_registry.hpp"

#include <cassert>
#include <glm/glm.hpp>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/voxel_volume.hpp>

TOAST_TEST_NAMED("voxel", "voxel/01-reshare_touches_only_changed_bricks", test_voxel_01_reshare_touches_only_changed_bricks) {
	voxel::BrickPool pool(16);
	voxel::Volume source(pool, glm::uvec3 {3, 1, 1});
	source.setVoxel({1, 1, 1}, 3);
	source.setVoxel({9, 1, 1}, 4);
	const uint32_t kept = source.entryAt({0, 0, 0}).payload();
	const uint32_t replaced = source.entryAt({1, 0, 0}).payload();

	voxel::Volume copy = voxel::Volume::instanceOf(source);
	assert(pool.referenceCount(kept) == 2 && pool.referenceCount(replaced) == 2);

	// copy on write gives brick 1 a new id while the copy still holds the old one
	source.setVoxel({10, 1, 1}, 4);
	const uint32_t written = source.entryAt({1, 0, 0}).payload();
	assert(written != replaced);

	copy.reshareFrom(source);
	assert(pool.referenceCount(kept) == 2);
	assert(pool.referenceCount(written) == 2);
	assert(pool.allocatedCount() == 2);    // the old brick 1 went back to the pool
	assert(copy.materialAt({10, 1, 1}) == 4 && copy.materialAt({1, 1, 1}) == 3);
	assert(copy.entryAt({0, 0, 0}).tag() == voxel::BrickTag::shared);

	// writing into a uniform brick reuses the freed slot uncleared, every byte must still come from the fill
	source.setBrickUniform({2, 0, 0}, 5);
	source.setVoxel({16, 0, 0}, 6);
	for (int32_t z = 0; z < 8; ++z) {
		for (int32_t y = 0; y < 8; ++y) {
			for (int32_t x = 16; x < 24; ++x) {
				assert(source.materialAt({x, y, z}) == (x == 16 && y == 0 && z == 0 ? 6 : 5));
			}
		}
	}
}
