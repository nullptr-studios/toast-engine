/**
 * @file surface.hpp
 * @author dario
 * @date 10/09/2026
 */

#pragma once
#include "brick.hpp"
#include "voxel_constants.hpp"
#include "voxel_volume.hpp"

#include <bit>
#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <toast/export.hpp>
#include <unordered_map>
#include <vector>

namespace toast::voxel {

enum class VoxelClass : uint8_t {
	empty = 0,
	inside = 1,
	face = 2,
	edge = 3,
	corner = 4,
};

inline constexpr uint8_t k_normal_direction_count = 26;

inline constexpr uint8_t k_normal_undefined = 26;

[[nodiscard]]
inline auto normalIndexOf(glm::ivec3 direction) noexcept -> uint8_t {
	if (direction.x == 0 && direction.y == 0 && direction.z == 0) {
		return k_normal_undefined;
	}
	const int raw = (direction.x + 1) + (direction.y + 1) * 3 + (direction.z + 1) * 9;
	return static_cast<uint8_t>(raw < 13 ? raw : raw - 1);
}

[[nodiscard]]
inline auto normalDirection(uint8_t index) noexcept -> glm::ivec3 {
	if (index >= k_normal_direction_count) {
		return glm::ivec3(0);
	}
	const int raw = index < 13 ? index : index + 1;
	return glm::ivec3(raw % 3 - 1, (raw / 3) % 3 - 1, raw / 9 - 1);
}

/// @brief Class in the low three bits and normal index in the high five
[[nodiscard]]
constexpr auto packClassification(VoxelClass type, uint8_t normal) noexcept -> uint8_t {
	return static_cast<uint8_t>(static_cast<uint8_t>(type) | (normal << 3));
}

[[nodiscard]]
constexpr auto classOf(uint8_t classification) noexcept -> VoxelClass {
	return static_cast<VoxelClass>(classification & 7u);
}

[[nodiscard]]
constexpr auto normalOf(uint8_t classification) noexcept -> uint8_t {
	return static_cast<uint8_t>(classification >> 3);
}

/// @brief Exposed faces that cancel (one voxel thick walls) promote to corner
[[nodiscard]]
inline auto classifyFromNeighbours(
    bool solid_neg_x, bool solid_pos_x, bool solid_neg_y, bool solid_pos_y, bool solid_neg_z, bool solid_pos_z
) noexcept -> uint8_t {
	const int exposed = static_cast<int>(!solid_neg_x) + static_cast<int>(!solid_pos_x) + static_cast<int>(!solid_neg_y) +
	                    static_cast<int>(!solid_pos_y) + static_cast<int>(!solid_neg_z) + static_cast<int>(!solid_pos_z);
	if (exposed == 0) {
		return packClassification(VoxelClass::inside, k_normal_undefined);
	}

	const glm::ivec3 direction(
	    static_cast<int>(!solid_pos_x) - static_cast<int>(!solid_neg_x),
	    static_cast<int>(!solid_pos_y) - static_cast<int>(!solid_neg_y),
	    static_cast<int>(!solid_pos_z) - static_cast<int>(!solid_neg_z)
	);
	const uint8_t normal = normalIndexOf(direction);

	VoxelClass type = VoxelClass::corner;
	if (normal != k_normal_undefined) {
		if (exposed == 1) {
			type = VoxelClass::face;
		} else if (exposed == 2) {
			type = VoxelClass::edge;
		}
	}
	return packClassification(type, normal);
}

struct SurfaceVoxel {
	/// localIndex which is also the occupancy bit index
	uint16_t local_index = 0;
	uint8_t classification = 0;
	uint8_t reserved = 0;

	[[nodiscard]]
	constexpr auto operator==(const SurfaceVoxel&) const noexcept -> bool = default;
};

static_assert(sizeof(SurfaceVoxel) == 4, "a surface list entry must be four bytes");

/// @brief Without @p neighbours every voxel on the outer brick faces reads as exposed
[[nodiscard]]
inline auto buildBrickSurface(const BrickOccupancy& brick, const BrickNeighbourhood& neighbours) -> std::vector<SurfaceVoxel> {
	const BrickOccupancy neg_x = neighboursNegX(brick, neighbours.neg_x);
	const BrickOccupancy pos_x = neighboursPosX(brick, neighbours.pos_x);
	const BrickOccupancy neg_y = neighboursNegY(brick, neighbours.neg_y);
	const BrickOccupancy pos_y = neighboursPosY(brick, neighbours.pos_y);
	const BrickOccupancy neg_z = neighboursNegZ(brick, neighbours.neg_z);
	const BrickOccupancy pos_z = neighboursPosZ(brick, neighbours.pos_z);

	const BrickOccupancy shell = brick & ~(neg_x & pos_x & neg_y & pos_y & neg_z & pos_z);

	std::vector<SurfaceVoxel> out;
	out.reserve(popCount(shell));
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		uint64_t word = shell[z];
		while (word != 0ull) {
			const auto bit = static_cast<uint32_t>(std::countr_zero(word));
			word &= word - 1ull;
			const uint64_t mask = 1ull << bit;

			SurfaceVoxel entry;
			entry.local_index = static_cast<uint16_t>(z * 64u + bit);
			entry.classification = classifyFromNeighbours(
			    (neg_x[z] & mask) != 0ull,
			    (pos_x[z] & mask) != 0ull,
			    (neg_y[z] & mask) != 0ull,
			    (pos_y[z] & mask) != 0ull,
			    (neg_z[z] & mask) != 0ull,
			    (pos_z[z] & mask) != 0ull
			);
			out.push_back(entry);
		}
	}
	return out;
}

/// @brief Keyed by brick slot not pool id since uniform bricks have no pool id and shared bricks differ per instance
class TOAST_API VolumeSurface {
public:
	void rebuild(const Volume& volume);

	void rebuildBrick(const Volume& volume, glm::ivec3 brick);

	void repairAround(const Volume& volume, glm::ivec3 voxel);

	void repairBrickRegion(const Volume& volume, glm::ivec3 brick);

	[[nodiscard]]
	auto brickSurface(glm::ivec3 brick) const -> std::span<const SurfaceVoxel>;

	[[nodiscard]]
	auto surfaceVoxelCount() const -> size_t;

	[[nodiscard]]
	auto populatedBrickCount() const noexcept -> size_t {
		return m_bricks.size();
	}

	[[nodiscard]]
	auto brickDims() const noexcept -> glm::uvec3 {
		return m_brick_dims;
	}

private:
	[[nodiscard]]
	auto contains(glm::ivec3 brick) const noexcept -> bool;

	[[nodiscard]]
	auto slotOf(glm::ivec3 brick) const noexcept -> uint32_t;

	glm::uvec3 m_brick_dims {0};

	std::unordered_map<uint32_t, std::vector<SurfaceVoxel>> m_bricks;
};

}
