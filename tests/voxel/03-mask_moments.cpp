#include "test_registry.hpp"

#include <cassert>
#include <toast/voxel/mass_accumulator.hpp>

TOAST_TEST_NAMED("voxel", "voxel/03-mask_moments", test_voxel_03_mask_moments) {
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
