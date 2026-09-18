#include "test_registry.hpp"
#include "voxel_test_utils.hpp"

#include <algorithm>
#include <cassert>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>

using namespace voxel;
using namespace voxeltest;

namespace {

[[nodiscard]]
auto matchesReference(const VolumeSurface& surface, const Volume& volume) -> bool {
	const auto solid = [&volume](glm::ivec3 v) { return volume.isSolidAt(v); };
	bool same = true;
	forEachCell(glm::ivec3(volume.brickDims()), [&](glm::ivec3 brick) {
		same = same && std::ranges::equal(surface.brickSurface(brick), referenceSurface(solid, brick * 8));
	});
	return same;
}

[[nodiscard]]
auto matchesRebuild(const VolumeSurface& repaired, const Volume& volume) -> bool {
	VolumeSurface fresh;
	fresh.rebuild(volume);
	bool same = repaired.populatedBrickCount() == fresh.populatedBrickCount();
	forEachCell(glm::ivec3(volume.brickDims()), [&](glm::ivec3 brick) {
		same = same && std::ranges::equal(repaired.brickSurface(brick), fresh.brickSurface(brick));
	});
	return same;
}

}

TOAST_TEST_NAMED("voxel", "voxel/10-surface-repair", test_voxel_10_surface_repair) {
	Rng rng {0x1357'9BDF'2468'ACE0ull};
	BrickPool pool(128);
	Volume volume(pool, glm::uvec3(3, 3, 3));
	volume.setBrickUniform(glm::ivec3(0), 2);
	volume.setBrickUniform(glm::ivec3(1), 7);
	forEachCell(glm::ivec3(3), [&](glm::ivec3 brick) {
		if (volume.entryAt(brick).tag() == BrickTag::empty && brick != glm::ivec3(2)) {
			fillRandom(volume, brick, rng, (brick.x + brick.y + brick.z) % 2 == 0 ? 20 : 70);
		}
	});

	VolumeSurface surface;
	surface.rebuild(volume);
	assert(matchesReference(surface, volume));
	assert(surface.brickSurface(glm::ivec3(2)).empty());

	for (uint32_t i = 0; i < 400; ++i) {
		glm::ivec3 voxel(0);
		for (int32_t a = 0; a < 3; ++a) {
			const uint32_t pick = rng.below(4);
			voxel[a] = static_cast<int32_t>(rng.below(3) * 8 + (pick == 0 ? 0 : pick == 1 ? 7 : rng.below(8)));
		}
		volume.setVoxel(voxel, rng.chance(50) ? k_empty_palette_index : static_cast<uint8_t>(1 + rng.below(9)));
		surface.repairAround(volume, voxel);
		assert(matchesRebuild(surface, volume));
	}

	for (const glm::ivec3& brick : {glm::ivec3(1, 0, 1), glm::ivec3(2), glm::ivec3(1)}) {
		for (uint8_t material : {uint8_t {4}, k_empty_palette_index}) {
			volume.setBrickUniform(brick, material);
			surface.repairBrickRegion(volume, brick);
			assert(matchesRebuild(surface, volume));
		}
	}
	assert(matchesReference(surface, volume));

	const size_t before = surface.surfaceVoxelCount();
	surface.repairAround(volume, glm::ivec3(-1, 0, 0));
	surface.repairAround(volume, glm::ivec3(0, 99, 0));
	assert(surface.surfaceVoxelCount() == before);

	{
		BrickPool shared_pool(8);
		Volume source(shared_pool, glm::uvec3(2, 1, 1));
		forEachCell(glm::ivec3(16, 8, 8), [&source](glm::ivec3 v) { source.setVoxel(v, 3); });
		Volume instance = Volume::instanceOf(source);
		instance.setVoxel(glm::ivec3(8, 3, 3), k_empty_palette_index);
		assert(instance.entryAt(glm::ivec3(0)).tag() == BrickTag::shared);

		VolumeSurface source_surface;
		source_surface.rebuild(source);
		VolumeSurface instance_surface;
		instance_surface.rebuild(instance);
		assert(matchesReference(source_surface, source) && matchesReference(instance_surface, instance));
	}
}
