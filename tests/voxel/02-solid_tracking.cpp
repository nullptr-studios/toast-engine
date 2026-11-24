#include "test_registry.hpp"

#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <toast/voxel/brick_pool.hpp>
#include <toast/voxel/connectivity.hpp>
#include <toast/voxel/mass_accumulator.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <vector>

namespace {

/// The cached counts and the solid brick walk against a recount from the occupancy
void checkTracking(const voxel::Volume& volume) {
	uint32_t total = 0;
	std::vector<uint32_t> solid_slots;
	for (uint32_t slot = 0; slot < volume.brickCount(); ++slot) {
		const glm::ivec3 brick = volume.brickAtIndex(slot);
		const voxel::BrickOccupancy* occupancy = volume.occupancyPointer(brick);
		const uint32_t count = occupancy != nullptr ? voxel::popCount(*occupancy) : 0u;
		assert(volume.brickSolidCount(brick) == count);
		total += count;
		if (count != 0) {
			solid_slots.push_back(slot);
		}
	}
	assert(volume.solidVoxelCount() == total);

	std::vector<uint32_t> walked;
	volume.forEachSolidBrick([&](glm::ivec3 brick, uint32_t slot) {
		assert(volume.brickAtIndex(slot) == brick);
		walked.push_back(slot);
	});
	assert(walked == solid_slots);
}

void checkSameConnectivity(const voxel::Connectivity& a, const voxel::Connectivity& b) {
	assert(a.component_count == b.component_count);
	assert(a.pieces.size() == b.pieces.size());
	for (size_t i = 0; i < a.pieces.size(); ++i) {
		assert(a.pieces[i].brick == b.pieces[i].brick);
		assert(a.pieces[i].voxels == b.pieces[i].voxels);
		assert(a.pieces[i].component == b.pieces[i].component);
	}
}

}

TOAST_TEST_NAMED("voxel", "voxel/02-solid_counts_follow_every_write", test_voxel_02_solid_counts_follow_every_write) {
	voxel::BrickPool pool(64);
	voxel::Volume volume(pool, glm::uvec3 {4, 2, 2});
	checkTracking(volume);

	// single voxels into empty, then out again so the brick frees
	volume.setVoxel({1, 1, 1}, 3);
	volume.setVoxel({2, 1, 1}, 3);
	volume.setVoxel({9, 3, 12}, 5);
	checkTracking(volume);
	volume.setVoxel({2, 1, 1}, 0);
	checkTracking(volume);

	// uniform bricks, written into and collapsed back
	volume.setBrickUniform({3, 1, 1}, 7);
	volume.setBrickUniform({2, 0, 0}, 4);
	checkTracking(volume);
	volume.setVoxel({16, 0, 0}, 0);
	checkTracking(volume);
	volume.setVoxel({16, 0, 0}, 4);
	assert(volume.tryCollapseUniform({2, 0, 0}));
	checkTracking(volume);

	// a mixed brick from bytes
	std::array<uint8_t, voxel::k_brick_material_bytes> bytes {};
	for (size_t i = 0; i < bytes.size(); i += 3) {
		bytes[i] = 2;
	}
	assert(volume.setBrickMaterial({0, 1, 0}, bytes));
	checkTracking(volume);

	// partial and whole clears
	voxel::BrickOccupancy half {};
	for (uint32_t z = 0; z < 4; ++z) {
		half[z] = ~0ull;
	}
	assert(volume.clearVoxels({3, 1, 1}, half));
	checkTracking(volume);
	assert(volume.clearVoxels({0, 1, 0}, voxel::k_full_brick));
	checkTracking(volume);

	// a write the caller does itself
	if (auto writable = volume.beginBrickWrite({1, 0, 1})) {
		writable->occupancy->slices[2] = 0xFFull;
		for (uint32_t x = 0; x < 8; ++x) {
			writable->material[(2 * 64) + x] = 6;
		}
	}
	volume.finishBrickWrite({1, 0, 1}, true);
	checkTracking(volume);

	// bricks moved between volumes and copied across
	voxel::Volume other(pool, glm::uvec3 {4, 2, 2});
	other.adoptBrick({0, 0, 0}, volume.takeBrick({1, 0, 1}));
	checkTracking(volume);
	checkTracking(other);
	assert(other.copyBrickFrom({1, 1, 1}, volume, {3, 1, 1}));
	assert(other.copyBrickFrom({2, 1, 1}, volume, {2, 0, 0}));
	checkTracking(other);

	// instances, reshare and resize keep the counts of what they copy
	voxel::Volume copy = voxel::Volume::instanceOf(volume);
	checkTracking(copy);
	volume.setVoxel({30, 15, 15}, 9);
	copy.reshareFrom(volume);
	checkTracking(copy);
	voxel::Volume resized = voxel::Volume::adoptResized(volume, {1, 0, 0}, glm::uvec3 {3, 2, 2});
	checkTracking(resized);
	checkTracking(volume);

	// the cache gives the same answer as a fresh analysis before and after a carve
	voxel::ConnectivityCache cache;
	checkSameConnectivity(voxel::analyseConnectivity(volume, &cache), voxel::analyseConnectivity(volume));
	volume.setVoxel({1, 1, 1}, 0);
	volume.setVoxel({25, 9, 9}, 0);
	checkSameConnectivity(voxel::analyseConnectivity(volume, &cache), voxel::analyseConnectivity(volume));
	checkSameConnectivity(voxel::analyseConnectivity(volume, &cache), voxel::analyseConnectivity(volume));
}

TOAST_TEST_NAMED("voxel", "voxel/03-mask_moments_match_a_voxel_walk", test_voxel_03_mask_moments_match_a_voxel_walk) {
	uint64_t state = 0x9E3779B97F4A7C15ull;
	const auto next = [&state] {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		return state;
	};
	for (int round = 0; round < 64; ++round) {
		voxel::BrickOccupancy mask {};
		for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
			// mixes sparse, dense and empty slices
			mask[z] = round % 3 == 0 ? next() & next() : (round % 3 == 1 ? next() | next() : (z % 2 == 0 ? next() : 0ull));
		}
		voxel::MassMoments walked;
		voxel::forEachSetVoxel(mask, [&](uint32_t, uint32_t x, uint32_t y, uint32_t z) {
			walked.add(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), 1);
		});
		assert(voxel::maskMoments(mask) == walked);
	}
	voxel::MassMoments full;
	voxel::forEachSetVoxel(voxel::k_full_brick, [&](uint32_t, uint32_t x, uint32_t y, uint32_t z) {
		full.add(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z), 1);
	});
	assert(voxel::maskMoments(voxel::k_full_brick) == full);
}
