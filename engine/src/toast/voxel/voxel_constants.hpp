/**
 * @file voxel_constants.hpp
 * @author dario
 * @date 08/09/2026
 */

#pragma once

#include <cstdint>

namespace voxel {

inline constexpr float k_voxel_size = 0.1f;

inline constexpr uint32_t k_brick_dim = 8;

inline constexpr uint32_t k_brick_voxel_count = k_brick_dim * k_brick_dim * k_brick_dim;

inline constexpr float k_brick_size = static_cast<float>(k_brick_dim) * k_voxel_size;

inline constexpr uint32_t k_region_dim_bricks = 32;

inline constexpr uint32_t k_region_dim_voxels = k_region_dim_bricks * k_brick_dim;

inline constexpr float k_region_size = static_cast<float>(k_region_dim_bricks) * k_brick_size;

inline constexpr uint32_t k_palette_size = 256;

inline constexpr uint8_t k_empty_palette_index = 0;

enum class BrickTag : uint8_t {
	empty = 0,
	/// The payload is a palette index not a brick id
	uniform = 1,
	/// Owned by the source asset so copy before writing
	shared = 2,
	owned = 3,
};

inline constexpr uint32_t k_brick_tag_bits = 2;

inline constexpr uint32_t k_brick_payload_mask = (1u << (32 - k_brick_tag_bits)) - 1u;

struct BrickEntry {
	uint32_t value = 0;

	[[nodiscard]]
	static constexpr auto make(BrickTag tag, uint32_t payload) noexcept -> BrickEntry {
		return BrickEntry {(payload & k_brick_payload_mask) << k_brick_tag_bits | static_cast<uint32_t>(tag)};
	}

	[[nodiscard]]
	constexpr auto tag() const noexcept -> BrickTag {
		return static_cast<BrickTag>(value & ((1u << k_brick_tag_bits) - 1u));
	}

	/// @returns a palette index for uniform and a pool id for shared or owned
	[[nodiscard]]
	constexpr auto payload() const noexcept -> uint32_t {
		return value >> k_brick_tag_bits;
	}

	[[nodiscard]]
	constexpr auto isPooled() const noexcept -> bool {
		const auto t = tag();
		return t == BrickTag::shared || t == BrickTag::owned;
	}

	[[nodiscard]]
	constexpr auto operator==(const BrickEntry&) const noexcept -> bool = default;
};

static_assert(k_brick_dim * k_brick_dim == 64, "a brick z-slice must be exactly 64 bits");
static_assert(k_brick_voxel_count == 512, "a brick must be 512 voxels");
static_assert(sizeof(BrickEntry) == 4, "an indirection entry must be four bytes");
static_assert(BrickEntry {}.tag() == BrickTag::empty, "a zeroed indirection grid must read as empty");

}
