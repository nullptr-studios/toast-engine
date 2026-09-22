#include "fragment_extraction.hpp"

#include "toast/voxel/runtime_pool.hpp"

#include <array>
#include <tracy/Tracy.hpp>

namespace physics {

auto extractFragmentVolume(const voxel::Volume& source, const DetachedComponent& component) -> ExtractedFragment {
	ZoneScopedN("physics::ExtractFragment");

	glm::ivec3 min {std::numeric_limits<int32_t>::max()};
	glm::ivec3 max {std::numeric_limits<int32_t>::min()};
	for (const auto& p : component.pieces) {
		min = glm::min(min, p.brick);
		max = glm::max(max, p.brick);
	}
	glm::uvec3 new_dimension = glm::uvec3(max - min) + glm::uvec3(1);

	voxel::Volume extracted(voxel::runtimeBrickPool(), new_dimension);

	// setBrickMaterial collapses a fully one material piece to uniform on its own
	std::array<uint8_t, voxel::k_brick_material_bytes> brick_material {};
	for (const voxel::BrickPiece& p : component.pieces) {
		brick_material.fill(voxel::k_empty_palette_index);

		for (uint32_t i = 0; i < voxel::k_brick_voxel_count; ++i) {
			if (!voxel::isSolid(p.voxels, i)) {
				continue;
			}

			const voxel::BrickCoord c = voxel::localFromIndex(i);
			const glm::ivec3 local_offset {static_cast<int32_t>(c.x), static_cast<int32_t>(c.y), static_cast<int32_t>(c.z)};
			const glm::ivec3 source_voxel = p.brick * static_cast<int32_t>(voxel::k_brick_dim) + local_offset;
			brick_material[i] = source.materialAt(source_voxel);
		}

		extracted.setBrickMaterial(p.brick - min, brick_material);
	}

	return ExtractedFragment {.volume = std::move(extracted), .offset = min};
}

}
