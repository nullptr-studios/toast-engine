/// @file voxel_test_utils.hpp

#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <stdexcept>
#include <toast/voxel/brick.hpp>
#include <toast/voxel/surface.hpp>
#include <toast/voxel/voxel_volume.hpp>
#include <vector>

namespace voxeltest {

using namespace toast::voxel;

inline const std::array<glm::ivec3, 6> k_steps {
  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),
};

struct Rng {
	uint64_t state = 0x9E3779B97F4A7C15ull;

	auto next() -> uint64_t {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return state * 0x2545F4914F6CDD1Dull;
	}

	auto below(uint32_t bound) -> uint32_t {
		return static_cast<uint32_t>(next() % bound);
	}

	auto chance(uint32_t percent) -> bool {
		return below(100) < percent;
	}

	auto uniform(float low, float high) -> float {
		return low + (high - low) * static_cast<float>(below(1'000'000)) / 1'000'000.0f;
	}
};

template<typename Visit>
void forEachCell(glm::ivec3 dims, Visit&& visit) {
	for (int32_t z = 0; z < dims.z; ++z) {
		for (int32_t y = 0; y < dims.y; ++y) {
			for (int32_t x = 0; x < dims.x; ++x) {
				visit(glm::ivec3(x, y, z));
			}
		}
	}
}

[[nodiscard]]
inline auto localVoxel(uint32_t index) -> glm::ivec3 {
	const BrickCoord c = localFromIndex(index);
	return glm::ivec3(static_cast<int32_t>(c.x), static_cast<int32_t>(c.y), static_cast<int32_t>(c.z));
}

[[nodiscard]]
inline auto isSolidAt(const BrickOccupancy& brick, glm::ivec3 voxel) -> bool {
	return isSolid(brick, static_cast<uint32_t>(voxel.x), static_cast<uint32_t>(voxel.y), static_cast<uint32_t>(voxel.z));
}

[[nodiscard]]
inline auto randomBrick(Rng& rng, uint32_t percent) -> BrickOccupancy {
	BrickOccupancy out;
	for (uint32_t index = 0; index < k_brick_voxel_count; ++index) {
		setSolid(out, index, rng.chance(percent));
	}
	return out;
}

inline void fillRandom(Volume& volume, glm::ivec3 brick, Rng& rng, uint32_t percent) {
	forEachCell(glm::ivec3(8), [&](glm::ivec3 local) {
		if (rng.chance(percent)) {
			volume.setVoxel(brick * 8 + local, static_cast<uint8_t>(1 + rng.below(9)));
		}
	});
}

/// Bit i of present keeps side i in BrickNeighbourhood member order
struct RandomSides {
	std::array<BrickOccupancy, 6> bricks {};
	uint32_t present = 0;

	RandomSides(Rng& rng, uint32_t percent, uint32_t present_mask) : present(present_mask) {
		for (BrickOccupancy& brick : bricks) {
			brick = randomBrick(rng, percent);
		}
	}

	[[nodiscard]]
	auto side(uint32_t face) const -> const BrickOccupancy* {
		return ((present >> face) & 1u) != 0 ? &bricks[face] : nullptr;
	}

	[[nodiscard]]
	auto view() const -> BrickNeighbourhood {
		return BrickNeighbourhood {side(0), side(1), side(2), side(3), side(4), side(5)};
	}
};

/// One step outside the brick reads the neighbour on that side or empty without one
[[nodiscard]]
inline auto solidAcross(const BrickOccupancy& brick, const BrickNeighbourhood& sides, glm::ivec3 voxel) -> bool {
	const std::array<const BrickOccupancy*, 6> by_face {sides.neg_x, sides.pos_x, sides.neg_y, sides.pos_y, sides.neg_z, sides.pos_z};
	for (int32_t a = 0; a < 3; ++a) {
		if (voxel[a] < 0 || voxel[a] >= 8) {
			const BrickOccupancy* side = by_face[static_cast<size_t>(a * 2 + (voxel[a] >= 8 ? 1 : 0))];
			return side != nullptr && isSolidAt(*side, (voxel + 8) % 8);
		}
	}
	return isSolidAt(brick, voxel);
}

/// Face and edge need one or two exposed faces that do not cancel and every other exposed voxel is a corner
template<typename Solid>
[[nodiscard]]
auto referenceSurface(Solid&& solid, glm::ivec3 brick_origin) -> std::vector<SurfaceVoxel> {
	std::vector<SurfaceVoxel> out;
	for (uint32_t index = 0; index < k_brick_voxel_count; ++index) {
		const glm::ivec3 voxel = brick_origin + localVoxel(index);
		if (!solid(voxel)) {
			continue;
		}

		int32_t exposed = 0;
		glm::ivec3 direction(0);
		for (const glm::ivec3& step : k_steps) {
			if (!solid(voxel + step)) {
				++exposed;
				direction += step;
			}
		}
		if (exposed == 0) {
			continue;
		}

		VoxelClass type = VoxelClass::corner;
		if (direction != glm::ivec3(0) && exposed <= 2) {
			type = exposed == 1 ? VoxelClass::face : VoxelClass::edge;
		}
		out.push_back(SurfaceVoxel {.local_index = static_cast<uint16_t>(index), .classification = packClassification(type, normalIndexOf(direction))});
	}
	return out;
}

template<typename Call>
[[nodiscard]]
auto throws(Call&& call) -> bool {
	try {
		call();
	} catch (const std::runtime_error&) {
		return true;
	}
	return false;
}

}
