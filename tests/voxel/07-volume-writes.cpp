#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <cassert>

using namespace toast::voxel;
using namespace voxeltest;

TOAST_TEST_NAMED("voxel", "voxel/07-volume-writes", test_voxel_07_volume_writes) {
	{
		BrickPool pool(8);
		Volume volume(pool, glm::uvec3(2, 3, 4));
		assert(volume.voxelDims() == glm::uvec3(16, 24, 32) && volume.brickCount() == 24);
		assert(volume.solidVoxelCount() == 0 && volume.occupancyPointer(glm::ivec3(1)) == nullptr);
		assert(!volume.containsVoxel(glm::ivec3(16, 0, 0)) && !volume.containsVoxel(glm::ivec3(-1, 0, 0)));
		assert(!volume.setVoxel(glm::ivec3(-1, 3, 3), 7).changed && pool.allocatedCount() == 0);
		assert(volume.materialAt(glm::ivec3(0, 0, 99)) == k_empty_palette_index);

		const Volume::VoxelWrite first = volume.setVoxel(glm::ivec3(9, 3, 4), 12);
		assert(first.changed && first.brick_became_occupied && first.previous_material == k_empty_palette_index);
		assert(volume.entryAt(glm::ivec3(1, 0, 0)).tag() == BrickTag::owned && pool.allocatedCount() == 1);
		assert(volume.materialAt(glm::ivec3(9, 3, 4)) == 12 && volume.materialAt(glm::ivec3(8, 3, 4)) == k_empty_palette_index);

		const Volume::VoxelWrite repeat = volume.setVoxel(glm::ivec3(9, 3, 4), 12);
		assert(!repeat.changed && repeat.previous_material == 12);

		const Volume::VoxelWrite second = volume.setVoxel(glm::ivec3(10, 3, 4), 5);
		assert(second.changed && !second.brick_became_occupied && volume.solidVoxelCount() == 2);

		assert(!volume.setVoxel(glm::ivec3(9, 3, 4), k_empty_palette_index).brick_became_empty);
		const Volume::VoxelWrite last = volume.setVoxel(glm::ivec3(10, 3, 4), k_empty_palette_index);
		assert(last.brick_became_empty && last.previous_material == 5);
		assert(volume.entryAt(glm::ivec3(1, 0, 0)).tag() == BrickTag::empty && pool.allocatedCount() == 0);
	}

	{
		BrickPool pool(8);
		Volume volume(pool, glm::uvec3(1, 1, 1));
		volume.setBrickUniform(glm::ivec3(0), 3);
		assert(volume.entryAt(glm::ivec3(0)).tag() == BrickTag::uniform && pool.allocatedCount() == 0);
		assert(volume.solidVoxelCount() == k_brick_voxel_count && volume.materialAt(glm::ivec3(7)) == 3);
		assert(isFull(*volume.occupancyPointer(glm::ivec3(0))));
		assert(!volume.setVoxel(glm::ivec3(4), 3).changed && pool.allocatedCount() == 0);

		const Volume::VoxelWrite diverge = volume.setVoxel(glm::ivec3(4), k_empty_palette_index);
		assert(diverge.changed && diverge.previous_material == 3 && !diverge.brick_became_empty);
		assert(volume.entryAt(glm::ivec3(0)).tag() == BrickTag::owned && pool.allocatedCount() == 1);
		assert(volume.solidVoxelCount() == k_brick_voxel_count - 1 && volume.materialAt(glm::ivec3(3, 4, 4)) == 3);

		assert(!volume.tryCollapseUniform(glm::ivec3(0)));
		volume.setVoxel(glm::ivec3(4), 5);
		assert(!volume.tryCollapseUniform(glm::ivec3(0)));
		volume.setVoxel(glm::ivec3(4), 3);
		assert(volume.tryCollapseUniform(glm::ivec3(0)));
		assert(volume.entryAt(glm::ivec3(0)).tag() == BrickTag::uniform && pool.allocatedCount() == 0);
	}

	{
		BrickPool pool(4);
		Volume volume(pool, glm::uvec3(3, 3, 3));
		forEachCell(glm::ivec3(3), [&volume](glm::ivec3 brick) {
			volume.setBrickUniform(brick, static_cast<uint8_t>(1 + brick.x + brick.y * 3 + brick.z * 9));
		});
		forEachCell(glm::ivec3(3), [&volume](glm::ivec3 brick) {
			assert(volume.materialAt(brick * 8 + 3) == 1 + brick.x + brick.y * 3 + brick.z * 9);
		});

		assert(isEmpty(surfaceShell(k_full_brick, volume.neighbourhoodOf(glm::ivec3(1)))));
		const BrickNeighbourhood corner = volume.neighbourhoodOf(glm::ivec3(0));
		assert(corner.neg_x == nullptr && corner.pos_x != nullptr && isFull(*corner.pos_x));
	}

	{
		BrickPool pool(4);
		{
			Volume volume(pool, glm::uvec3(2, 2, 2));
			volume.setVoxel(glm::ivec3(0), 1);
			volume.setVoxel(glm::ivec3(8, 0, 0), 1);
			assert(pool.allocatedCount() == 2);
		}
		assert(pool.allocatedCount() == 0);
	}
}
