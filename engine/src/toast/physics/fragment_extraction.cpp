#include "fragment_extraction.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <tracy/Tracy.hpp>
#include <tuple>
#include <utility>
#include <vector>

namespace physics {

namespace {

auto unitBrickMoments() -> const voxel::MassMoments& {
	static const voxel::MassMoments unit = [] {
		voxel::MassMoments moments;
		for (int32_t z = 0; z < static_cast<int32_t>(voxel::k_brick_dim); ++z) {
			for (int32_t y = 0; y < static_cast<int32_t>(voxel::k_brick_dim); ++y) {
				for (int32_t x = 0; x < static_cast<int32_t>(voxel::k_brick_dim); ++x) {
					moments.add(x, y, z, 1);
				}
			}
		}
		return moments;
	}();
	return unit;
}

template<typename Callback>
void forEachSetVoxel(const voxel::BrickOccupancy& mask, Callback&& callback) {
	for (uint32_t z = 0; z < voxel::k_brick_dim; ++z) {
		uint64_t word = mask[z];
		while (word != 0ull) {
			const auto bit = static_cast<uint32_t>(std::countr_zero(word));
			word &= word - 1ull;
			callback((z * voxel::k_brick_dim * voxel::k_brick_dim) + bit, bit & 7u, bit >> 3u, z);
		}
	}
}

auto materialOf(const voxel::Volume& source, voxel::BrickEntry entry, uint32_t index) -> uint8_t {
	return entry.tag() == voxel::BrickTag::uniform ? static_cast<uint8_t>(entry.payload())
	                                               : source.pool()->material(entry.payload())[index];
}

auto maskedMoments(
    const voxel::Volume& source, voxel::BrickEntry entry, const voxel::BrickOccupancy& mask, bool whole, glm::ivec3 origin,
    const DensityTable& density
) -> voxel::MassMoments {
	if (whole && entry.tag() == voxel::BrickTag::uniform) {
		return unitBrickMoments().scaled(density[entry.payload()]).shifted(origin.x, origin.y, origin.z);
	}

	voxel::MassMoments moments;
	forEachSetVoxel(mask, [&](uint32_t index, uint32_t x, uint32_t y, uint32_t z) {
		moments.add(
		    origin.x + static_cast<int32_t>(x),
		    origin.y + static_cast<int32_t>(y),
		    origin.z + static_cast<int32_t>(z),
		    density[materialOf(source, entry, index)]
		);
	});
	return moments;
}

struct BrickMask {
	glm::ivec3 brick {0};
	voxel::BrickOccupancy voxels {};
};

/// Pieces of one brick merge so each brick is handled once
auto mergedMasks(const DetachedComponent& component) -> std::vector<BrickMask> {
	std::vector<BrickMask> masks;
	masks.reserve(component.pieces.size());
	for (const voxel::BrickPiece& p : component.pieces) {
		masks.push_back(BrickMask {.brick = p.brick, .voxels = p.voxels});
	}
	std::ranges::sort(masks, {}, [](const BrickMask& m) { return std::tuple(m.brick.z, m.brick.y, m.brick.x); });

	size_t out = 0;
	for (size_t i = 0; i < masks.size(); ++i) {
		if (out > 0 && masks[out - 1].brick == masks[i].brick) {
			masks[out - 1].voxels = masks[out - 1].voxels | masks[i].voxels;
		} else {
			masks[out++] = masks[i];
		}
	}
	masks.resize(out);
	return masks;
}

}

auto extractFragmentVolume(voxel::Volume& source, const DetachedComponent& component, const DensityTable& density)
    -> std::optional<ExtractedFragment> {
	ZoneScopedN("physics::ExtractFragment");

	if (component.pieces.empty()) {
		return std::nullopt;
	}

	const std::vector<BrickMask> masks = mergedMasks(component);
	glm::ivec3 min {std::numeric_limits<int32_t>::max()};
	glm::ivec3 max {std::numeric_limits<int32_t>::min()};
	for (const BrickMask& m : masks) {
		min = glm::min(min, m.brick);
		max = glm::max(max, m.brick);
	}

	voxel::BrickPool& pool = *source.pool();

	// Whole owned bricks move and every other brick needs a new or writable brick
	uint32_t bricks_needed = 0;
	for (const BrickMask& m : masks) {
		const voxel::BrickOccupancy* occupancy = source.occupancyPointer(m.brick);
		if (occupancy == nullptr || voxel::isEmpty(m.voxels & *occupancy)) {
			continue;
		}
		const voxel::BrickEntry entry = source.entryAt(m.brick);
		if ((m.voxels & *occupancy) == *occupancy) {
			bricks_needed += entry.tag() == voxel::BrickTag::shared ? 1u : 0u;
		} else {
			bricks_needed += 1u + (entry.tag() == voxel::BrickTag::owned ? 0u : 1u);
		}
	}
	if (pool.freeCount() < bricks_needed) {
		return std::nullopt;
	}

	voxel::Volume fragment(pool, glm::uvec3(max - min) + glm::uvec3(1));
	voxel::MassMoments moments;
	uint32_t voxels = 0;
	std::array<uint8_t, voxel::k_brick_material_bytes> bytes {};

	for (const BrickMask& m : masks) {
		const voxel::BrickOccupancy* occupancy = source.occupancyPointer(m.brick);
		if (occupancy == nullptr) {
			continue;
		}
		const voxel::BrickOccupancy mask = m.voxels & *occupancy;
		if (voxel::isEmpty(mask)) {
			continue;
		}

		const voxel::BrickEntry entry = source.entryAt(m.brick);
		const glm::ivec3 local = m.brick - min;
		const bool whole = mask == *occupancy;

		moments += maskedMoments(source, entry, mask, whole, local * static_cast<int32_t>(voxel::k_brick_dim), density);
		voxels += voxel::popCount(mask);

		if (whole && entry.tag() == voxel::BrickTag::uniform) {
			fragment.setBrickUniform(local, static_cast<uint8_t>(entry.payload()));
			static_cast<void>(source.takeBrick(m.brick));
			continue;
		}
		if (whole && entry.tag() == voxel::BrickTag::owned) {
			fragment.adoptBrick(local, source.takeBrick(m.brick));
			continue;
		}

		bytes.fill(voxel::k_empty_palette_index);
		forEachSetVoxel(mask, [&](uint32_t index, uint32_t, uint32_t, uint32_t) { bytes[index] = materialOf(source, entry, index); });
		fragment.setBrickMaterial(local, bytes);

		if (whole) {
			static_cast<void>(source.takeBrick(m.brick));
		} else {
			source.clearVoxels(m.brick, mask);
		}
	}

	return ExtractedFragment {.volume = std::move(fragment), .offset = min, .moments = moments, .voxels = voxels};
}

void restoreFragment(voxel::Volume& source, voxel::Volume& fragment, glm::ivec3 offset) {
	ZoneScopedN("physics::RestoreFragment");

	const glm::uvec3 dims = fragment.brickDims();
	for (int32_t z = 0; std::cmp_less(z, dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, dims.x); ++x) {
				const glm::ivec3 local {x, y, z};
				const voxel::BrickEntry entry = fragment.entryAt(local);
				if (entry.tag() == voxel::BrickTag::empty) {
					continue;
				}

				const glm::ivec3 brick = local + offset;
				if (source.entryAt(brick).tag() == voxel::BrickTag::empty) {
					if (entry.tag() == voxel::BrickTag::uniform) {
						source.setBrickUniform(brick, static_cast<uint8_t>(entry.payload()));
					} else {
						source.adoptBrick(brick, fragment.takeBrick(local));
					}
					continue;
				}

				const voxel::BrickOccupancy* occupancy = fragment.occupancyPointer(local);
				forEachSetVoxel(*occupancy, [&](uint32_t index, uint32_t vx, uint32_t vy, uint32_t vz) {
					const glm::ivec3 voxel_pos = (brick * static_cast<int32_t>(voxel::k_brick_dim)) +
					                             glm::ivec3(static_cast<int32_t>(vx), static_cast<int32_t>(vy), static_cast<int32_t>(vz));
					source.setVoxel(voxel_pos, materialOf(fragment, entry, index));
				});
			}
		}
	}
}

auto discardComponent(voxel::Volume& source, const DetachedComponent& component, const DensityTable& density) -> RemovedVoxels {
	ZoneScopedN("physics::DiscardComponent");

	RemovedVoxels removed;
	for (const BrickMask& m : mergedMasks(component)) {
		const voxel::BrickOccupancy* occupancy = source.occupancyPointer(m.brick);
		if (occupancy == nullptr) {
			continue;
		}
		const voxel::BrickOccupancy mask = m.voxels & *occupancy;
		if (voxel::isEmpty(mask)) {
			continue;
		}

		const voxel::BrickEntry entry = source.entryAt(m.brick);
		const bool whole = mask == *occupancy;
		const voxel::MassMoments moments =
		    maskedMoments(source, entry, mask, whole, m.brick * static_cast<int32_t>(voxel::k_brick_dim), density);
		const uint32_t count = voxel::popCount(mask);

		if (whole) {
			const voxel::BrickEntry taken = source.takeBrick(m.brick);
			if (taken.tag() == voxel::BrickTag::owned) {
				source.pool()->free(taken.payload());
			}
		} else if (!source.clearVoxels(m.brick, mask)) {
			continue;
		}

		removed.moments += moments;
		removed.voxels += count;
	}
	return removed;
}

}
