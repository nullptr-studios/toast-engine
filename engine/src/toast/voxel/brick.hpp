/**
 * @file brick.hpp
 * @author dario
 * @date 08/09/2026
 */

#pragma once
#include "voxel_constants.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>

namespace toast::voxel {

/// @brief One 64-bit word per z-slice with bit y * 8 + x
struct BrickOccupancy {
	std::array<uint64_t, k_brick_dim> slices {};

	[[nodiscard]]
	constexpr auto operator[](uint32_t z) noexcept -> uint64_t& {
		assert(z < k_brick_dim);
		return slices[z];
	}

	[[nodiscard]]
	constexpr auto operator[](uint32_t z) const noexcept -> const uint64_t& {
		assert(z < k_brick_dim);
		return slices[z];
	}

	[[nodiscard]]
	constexpr auto operator==(const BrickOccupancy&) const noexcept -> bool = default;
};

inline constexpr BrickOccupancy k_empty_brick {};

inline constexpr BrickOccupancy k_full_brick {
  {~0ull, ~0ull, ~0ull, ~0ull, ~0ull, ~0ull, ~0ull, ~0ull}
};

inline constexpr uint64_t k_column_x_min = 0x0101010101010101ull;

inline constexpr uint64_t k_column_x_max = 0x8080808080808080ull;

inline constexpr uint64_t k_row_y_min = 0x00000000000000FFull;

inline constexpr uint64_t k_row_y_max = 0xFF00000000000000ull;

[[nodiscard]]
constexpr auto localIndex(const uint32_t x, const uint32_t y, const uint32_t z) noexcept -> uint32_t {
	assert(x < k_brick_dim && y < k_brick_dim && z < k_brick_dim);
	return x + (y * k_brick_dim) + (z * k_brick_dim * k_brick_dim);
}

struct BrickCoord {
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t z = 0;

	[[nodiscard]]
	constexpr auto operator==(const BrickCoord&) const noexcept -> bool = default;
};

[[nodiscard]]
constexpr auto localFromIndex(uint32_t index) noexcept -> BrickCoord {
	assert(index < k_brick_voxel_count);
	return BrickCoord {index & 7u, (index >> 3u) & 7u, index >> 6u};
}

[[nodiscard]]
constexpr auto sliceBit(uint32_t x, uint32_t y) noexcept -> uint32_t {
	assert(x < k_brick_dim && y < k_brick_dim);
	return y * k_brick_dim + x;
}

[[nodiscard]]
constexpr auto isSolid(const BrickOccupancy& brick, uint32_t x, uint32_t y, uint32_t z) noexcept -> bool {
	assert(z < k_brick_dim);
	return ((brick[z] >> sliceBit(x, y)) & 1ull) != 0ull;
}

[[nodiscard]]
constexpr auto isSolid(const BrickOccupancy& brick, uint32_t local_index) noexcept -> bool {
	const BrickCoord c = localFromIndex(local_index);
	return isSolid(brick, c.x, c.y, c.z);
}

constexpr void setSolid(BrickOccupancy& brick, uint32_t x, uint32_t y, uint32_t z, bool solid) noexcept {
	assert(z < k_brick_dim);
	const uint64_t bit = 1ull << sliceBit(x, y);
	if (solid) {
		brick[z] |= bit;
	} else {
		brick[z] &= ~bit;
	}
}

constexpr void setSolid(BrickOccupancy& brick, uint32_t local_index, bool solid) noexcept {
	const BrickCoord c = localFromIndex(local_index);
	setSolid(brick, c.x, c.y, c.z, solid);
}

[[nodiscard]]
constexpr auto isEmpty(const BrickOccupancy& brick) noexcept -> bool {
	for (uint64_t slice : brick.slices) {
		if (slice != 0ull) {
			return false;
		}
	}
	return true;
}

[[nodiscard]]
constexpr auto isFull(const BrickOccupancy& brick) noexcept -> bool {
	for (uint64_t slice : brick.slices) {
		if (slice != ~0ull) {
			return false;
		}
	}
	return true;
}

[[nodiscard]]
constexpr auto popCount(const BrickOccupancy& brick) noexcept -> uint32_t {
	uint32_t total = 0;
	for (uint64_t slice : brick.slices) {
		total += static_cast<uint32_t>(std::popcount(slice));
	}
	return total;
}

[[nodiscard]]
constexpr auto operator&(const BrickOccupancy& a, const BrickOccupancy& b) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = a[z] & b[z];
	}
	return out;
}

[[nodiscard]]
constexpr auto operator|(const BrickOccupancy& a, const BrickOccupancy& b) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = a[z] | b[z];
	}
	return out;
}

[[nodiscard]]
constexpr auto operator~(const BrickOccupancy& a) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = ~a[z];
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursNegX(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = (brick[z] & ~k_column_x_max) << 1;
		if (adjacent != nullptr) {
			out[z] |= ((*adjacent)[z] >> 7) & k_column_x_min;
		}
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursPosX(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = (brick[z] & ~k_column_x_min) >> 1;
		if (adjacent != nullptr) {
			out[z] |= ((*adjacent)[z] << 7) & k_column_x_max;
		}
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursNegY(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = brick[z] << k_brick_dim;
		if (adjacent != nullptr) {
			out[z] |= (*adjacent)[z] >> 56;
		}
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursPosY(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out[z] = brick[z] >> k_brick_dim;
		if (adjacent != nullptr) {
			out[z] |= (*adjacent)[z] << 56;
		}
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursNegZ(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	out[0] = adjacent != nullptr ? (*adjacent)[k_brick_dim - 1] : 0ull;
	for (uint32_t z = 1; z < k_brick_dim; ++z) {
		out[z] = brick[z - 1];
	}
	return out;
}

[[nodiscard]]
constexpr auto neighboursPosZ(const BrickOccupancy& brick, const BrickOccupancy* adjacent = nullptr) noexcept -> BrickOccupancy {
	BrickOccupancy out {};
	for (uint32_t z = 0; z + 1 < k_brick_dim; ++z) {
		out[z] = brick[z + 1];
	}
	out[k_brick_dim - 1] = adjacent != nullptr ? (*adjacent)[0] : 0ull;
	return out;
}

struct BrickNeighbourhood {
	const BrickOccupancy* neg_x = nullptr;
	const BrickOccupancy* pos_x = nullptr;
	const BrickOccupancy* neg_y = nullptr;
	const BrickOccupancy* pos_y = nullptr;
	const BrickOccupancy* neg_z = nullptr;
	const BrickOccupancy* pos_z = nullptr;
};

[[nodiscard]]
constexpr auto interior(const BrickOccupancy& brick, const BrickNeighbourhood& neighbours = {}) noexcept -> BrickOccupancy {
	return brick & neighboursNegX(brick, neighbours.neg_x) & neighboursPosX(brick, neighbours.pos_x) &
	       neighboursNegY(brick, neighbours.neg_y) & neighboursPosY(brick, neighbours.pos_y) &
	       neighboursNegZ(brick, neighbours.neg_z) & neighboursPosZ(brick, neighbours.pos_z);
}

[[nodiscard]]
constexpr auto surfaceShell(const BrickOccupancy& brick, const BrickNeighbourhood& neighbours = {}) noexcept -> BrickOccupancy {
	return brick & ~interior(brick, neighbours);
}

[[nodiscard]]
constexpr auto dilate(const BrickOccupancy& brick) noexcept -> BrickOccupancy {
	return brick | neighboursNegX(brick) | neighboursPosX(brick) | neighboursNegY(brick) | neighboursPosY(brick) |
	       neighboursNegZ(brick) | neighboursPosZ(brick);
}

/// @brief x == 0 plane with bit z * 8 + y
[[nodiscard]]
constexpr auto faceNegX(const BrickOccupancy& brick) noexcept -> uint64_t {
	uint64_t out = 0;
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		for (uint32_t y = 0; y < k_brick_dim; ++y) {
			if (((brick[z] >> sliceBit(0, y)) & 1ull) != 0ull) {
				out |= 1ull << (z * k_brick_dim + y);
			}
		}
	}
	return out;
}

/// @brief x == 7 plane with bit z * 8 + y
[[nodiscard]]
constexpr auto facePosX(const BrickOccupancy& brick) noexcept -> uint64_t {
	uint64_t out = 0;
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		for (uint32_t y = 0; y < k_brick_dim; ++y) {
			if (((brick[z] >> sliceBit(k_brick_dim - 1, y)) & 1ull) != 0ull) {
				out |= 1ull << (z * k_brick_dim + y);
			}
		}
	}
	return out;
}

/// @brief y == 0 plane with bit z * 8 + x
[[nodiscard]]
constexpr auto faceNegY(const BrickOccupancy& brick) noexcept -> uint64_t {
	uint64_t out = 0;
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out |= (brick[z] & k_row_y_min) << (z * k_brick_dim);
	}
	return out;
}

/// @brief y == 7 plane with bit z * 8 + x
[[nodiscard]]
constexpr auto facePosY(const BrickOccupancy& brick) noexcept -> uint64_t {
	uint64_t out = 0;
	for (uint32_t z = 0; z < k_brick_dim; ++z) {
		out |= (brick[z] >> 56) << (z * k_brick_dim);
	}
	return out;
}

/// @brief z == 0 plane with bit y * 8 + x
[[nodiscard]]
constexpr auto faceNegZ(const BrickOccupancy& brick) noexcept -> uint64_t {
	return brick[0];
}

/// @brief z == 7 plane with bit y * 8 + x
[[nodiscard]]
constexpr auto facePosZ(const BrickOccupancy& brick) noexcept -> uint64_t {
	return brick[k_brick_dim - 1];
}

struct BrickFaces {
	uint64_t neg_x = 0;
	uint64_t pos_x = 0;
	uint64_t neg_y = 0;
	uint64_t pos_y = 0;
	uint64_t neg_z = 0;
	uint64_t pos_z = 0;

	[[nodiscard]]
	constexpr auto operator==(const BrickFaces&) const noexcept -> bool = default;
};

[[nodiscard]]
constexpr auto computeFaces(const BrickOccupancy& brick) noexcept -> BrickFaces {
	return BrickFaces {faceNegX(brick), facePosX(brick), faceNegY(brick), facePosY(brick), faceNegZ(brick), facePosZ(brick)};
}

[[nodiscard]]
constexpr auto facesConnect(uint64_t face_a, uint64_t face_b) noexcept -> bool {
	return (face_a & face_b) != 0ull;
}

}
