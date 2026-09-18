#include "test_registry.hpp"

#include <cassert>
#include <toast/voxel/voxel_constants.hpp>

using namespace toast::voxel;

TOAST_TEST_NAMED("voxel", "voxel/04-indirection-entry", test_voxel_04_indirection_entry) {
	assert(BrickEntry {}.tag() == BrickTag::empty && BrickEntry {}.payload() == 0);

	for (BrickTag tag : {BrickTag::empty, BrickTag::uniform, BrickTag::shared, BrickTag::owned}) {
		for (uint32_t payload : {0u, 1u, 255u, 0xFFFFu, k_brick_payload_mask}) {
			const BrickEntry entry = BrickEntry::make(tag, payload);
			assert(entry.tag() == tag && entry.payload() == payload);
			assert(entry.value == (payload << 2 | static_cast<uint32_t>(tag)));
			assert(entry.isPooled() == (tag == BrickTag::shared || tag == BrickTag::owned));
		}
	}

	const BrickEntry overflowed = BrickEntry::make(BrickTag::owned, 0xFFFFFFFFu);
	assert(overflowed.tag() == BrickTag::owned && overflowed.payload() == k_brick_payload_mask);
	assert(!(BrickEntry::make(BrickTag::shared, 42) == BrickEntry::make(BrickTag::owned, 42)));
}
