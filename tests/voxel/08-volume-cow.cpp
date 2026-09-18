#include "test_registry.hpp"

#include <cassert>
#include <toast/voxel/voxel_volume.hpp>
#include <utility>

using namespace voxel;

namespace {

/// Bricks 0 to 2 hold material 10 to 12 on their first four voxels of row zero
[[nodiscard]]
auto makeSource(BrickPool& pool) -> Volume {
	Volume source(pool, glm::uvec3(3, 1, 1));
	for (int32_t brick = 0; brick < 3; ++brick) {
		for (int32_t x = 0; x < 4; ++x) {
			source.setVoxel(glm::ivec3(brick * 8 + x, 0, 0), static_cast<uint8_t>(10 + brick));
		}
	}
	return source;
}

}

TOAST_TEST_NAMED("voxel", "voxel/08-volume-cow", test_voxel_08_volume_cow) {
	{
		BrickPool pool(8);
		const Volume source = makeSource(pool);
		Volume instance = Volume::instanceOf(source);
		assert(pool.allocatedCount() == 3 && instance.sharedBrickCount() == 3 && instance.ownedBrickCount() == 0);
		assert(instance.solidVoxelCount() == source.solidVoxelCount() && instance.materialAt(glm::ivec3(9, 0, 0)) == 11);

		assert(!instance.setVoxel(glm::ivec3(8, 0, 0), 11).changed);
		assert(!instance.setVoxel(glm::ivec3(7, 0, 0), k_empty_palette_index).changed);
		assert(pool.allocatedCount() == 3);

		assert(instance.setVoxel(glm::ivec3(9, 0, 0), 99).previous_material == 11);
		assert(pool.allocatedCount() == 4 && instance.ownedBrickCount() == 1 && instance.sharedBrickCount() == 2);
		assert(instance.entryAt(glm::ivec3(1, 0, 0)).tag() == BrickTag::owned);
		assert(instance.materialAt(glm::ivec3(9, 0, 0)) == 99 && instance.materialAt(glm::ivec3(10, 0, 0)) == 11);
		assert(source.materialAt(glm::ivec3(9, 0, 0)) == 11);

		for (int32_t x = 0; x < 4; ++x) {
			instance.setVoxel(glm::ivec3(x, 0, 0), k_empty_palette_index);
		}
		assert(instance.entryAt(glm::ivec3(0)).tag() == BrickTag::empty && pool.allocatedCount() == 4);
		assert(source.materialAt(glm::ivec3(0)) == 10);
	}

	{
		BrickPool pool(8);
		const Volume source = makeSource(pool);
		{
			Volume a = Volume::instanceOf(source);
			Volume b = Volume::instanceOf(source);
			a.setVoxel(glm::ivec3(0), 77);
			b.setVoxel(glm::ivec3(0), 88);
			assert(pool.allocatedCount() == 5);
			assert(a.materialAt(glm::ivec3(0)) == 77 && b.materialAt(glm::ivec3(0)) == 88 && source.materialAt(glm::ivec3(0)) == 10);
			assert(a.entryAt(glm::ivec3(1, 0, 0)) == b.entryAt(glm::ivec3(1, 0, 0)));
		}
		assert(pool.allocatedCount() == 3);
	}

	{
		BrickPool pool(4);
		Volume source(pool, glm::uvec3(1, 1, 1));
		source.setBrickUniform(glm::ivec3(0), 4);
		Volume instance = Volume::instanceOf(source);
		assert(instance.entryAt(glm::ivec3(0)).tag() == BrickTag::uniform && pool.allocatedCount() == 0);

		instance.setVoxel(glm::ivec3(3), k_empty_palette_index);
		assert(instance.entryAt(glm::ivec3(0)).tag() == BrickTag::owned && pool.allocatedCount() == 1);
		assert(source.entryAt(glm::ivec3(0)).tag() == BrickTag::uniform);
	}

	{
		BrickPool pool(4);
		Volume first(pool, glm::uvec3(1, 1, 1));
		first.setVoxel(glm::ivec3(0), 7);
		{
			const Volume second = std::move(first);
			assert(pool.allocatedCount() == 1 && second.materialAt(glm::ivec3(0)) == 7);
		}
		assert(pool.allocatedCount() == 0);
	}
}
