#include "stamp.hpp"

#include <algorithm>
#include <cassert>
#include <tracy/Tracy.hpp>
#include <unordered_set>
#include <utility>

namespace toast::voxel {

auto stamp(Volume& target, const Volume& piece, const LatticePlacement& placement, const PaletteRemapTable& remap)
    -> StampResult {
	ZoneScoped;
	assert(&target != &piece && "stamping a volume into itself would read what it is writing");

	StampResult result;
	const glm::uvec3 dims = piece.brickDims();
	const glm::uvec3 target_dims = target.brickDims();
	const LatticeOrientation& orientation = placement.orientation;

	std::unordered_set<uint32_t> touched_slots;
	std::vector<glm::ivec3> touched;
	const auto touch = [&](glm::ivec3 brick) {
		const uint32_t slot = static_cast<uint32_t>(brick.x) + (static_cast<uint32_t>(brick.y) * target_dims.x) +
		                      (static_cast<uint32_t>(brick.z) * target_dims.x * target_dims.y);
		if (touched_slots.insert(slot).second) {
			touched.push_back(brick);
		}
	};

	for (int32_t z = 0; std::cmp_less(z, dims.z); ++z) {
		for (int32_t y = 0; std::cmp_less(y, dims.y); ++y) {
			for (int32_t x = 0; std::cmp_less(x, dims.x); ++x) {
				const glm::ivec3 brick(x, y, z);
				const BrickEntry entry = piece.entryAt(brick);
				if (entry.tag() == BrickTag::empty) {
					continue;
				}

				if (entry.tag() == BrickTag::uniform) {
					glm::ivec3 corner;
					for (int axis = 0; axis < 3; ++axis) {
						const int32_t start = brick[orientation.source[axis]] * static_cast<int32_t>(k_brick_dim);
						corner[axis] = placement.offset[axis] + (orientation.flip[axis] ? -start - 8 : start);
					}
					const glm::ivec3 image(corner.x >> 3, corner.y >> 3, corner.z >> 3);
					const bool aligned = (corner.x & 7) == 0 && (corner.y & 7) == 0 && (corner.z & 7) == 0;
					if (aligned && target.containsBrick(image)) {
						const uint8_t mapped = remap[entry.payload()];
						if (mapped == k_empty_palette_index) {
							result.voxels_unmapped += k_brick_voxel_count;
						} else {
							target.setBrickUniform(image, mapped);
							result.voxels_written += k_brick_voxel_count;
						}
						continue;
					}
				}

				std::array<uint8_t, k_brick_material_bytes> bytes {};
				if (entry.tag() == BrickTag::uniform) {
					bytes.fill(static_cast<uint8_t>(entry.payload()));
				} else {
					const std::span<const uint8_t, k_brick_material_bytes> source = piece.pool()->material(entry.payload());
					std::copy(source.begin(), source.end(), bytes.begin());
				}

				for (uint32_t i = 0; i < k_brick_voxel_count; ++i) {
					const uint8_t material = bytes[i];
					if (material == k_empty_palette_index) {
						continue;
					}
					const uint8_t mapped = remap[material];
					if (mapped == k_empty_palette_index) {
						++result.voxels_unmapped;
						continue;
					}

					const glm::ivec3 voxel =
					    brick * static_cast<int32_t>(k_brick_dim) +
					    glm::ivec3(static_cast<int32_t>(i & 7u), static_cast<int32_t>((i >> 3) & 7u), static_cast<int32_t>(i >> 6));
					const glm::ivec3 landed = placeVoxel(placement, voxel);
					if (!target.containsVoxel(landed)) {
						++result.voxels_clipped;
						continue;
					}

					target.setVoxel(landed, mapped);
					// setVoxel reports a no-op and an exhausted pool the same way
					if (target.materialAt(landed) != mapped) {
						result.pool_exhausted = true;
						return result;
					}
					++result.voxels_written;
					touch(glm::ivec3(landed.x >> 3, landed.y >> 3, landed.z >> 3));
				}
			}
		}
	}

	for (const glm::ivec3& brick : touched) {
		target.tryCollapseUniform(brick);
	}
	return result;
}

}
