/**
 * @file anchor_mask.hpp
 * @author Xein
 * @date 19 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

namespace physics {

enum class AnchorFace : uint8_t {
	neg_x = 0b000001,
	pos_x = 0b000010,
	neg_y = 0b000100,
	pos_y = 0b001000,
	neg_z = 0b010000,
	pos_z = 0b100000,
};

using AnchorMask = uint8_t;
inline constexpr AnchorMask k_anchor_null = 0;
inline constexpr AnchorMask k_anchor_bottom = static_cast<AnchorMask>(AnchorFace::neg_z);

constexpr auto anchorFaceBit(AnchorFace face) -> AnchorMask {
	return static_cast<AnchorMask>(face);
}

constexpr auto hasAnchorFace(AnchorMask mask, AnchorFace face) -> bool {
	return (mask & anchorFaceBit(face)) != 0;
}

}
