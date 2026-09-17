/**
 * @file stamp.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "palette.hpp"
#include "voxel_constants.hpp"
#include "voxel_volume.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <toast/export.hpp>
#include <vector>

namespace voxel {

/// @brief Output axis i reads input axis source[i] negated when flip[i]
struct LatticeOrientation {
	std::array<uint8_t, 3> source {0, 1, 2};
	std::array<bool, 3> flip {false, false, false};

	[[nodiscard]]
	constexpr auto operator==(const LatticeOrientation&) const noexcept -> bool = default;

	[[nodiscard]]
	constexpr auto isMirror() const noexcept -> bool {
		// Permutation parity times one sign flip per negated axis
		int inversions = 0;
		for (int i = 0; i < 3; ++i) {
			for (int j = i + 1; j < 3; ++j) {
				inversions += source[i] > source[j] ? 1 : 0;
			}
		}
		const int negated = static_cast<int>(flip[0]) + static_cast<int>(flip[1]) + static_cast<int>(flip[2]);
		return (inversions + negated) % 2 != 0;
	}
};

[[nodiscard]]
inline auto allLatticeOrientations() -> std::array<LatticeOrientation, 48> {
	constexpr std::array<std::array<uint8_t, 3>, 6> permutations {
	  {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}
	};

	std::array<LatticeOrientation, 48> out {};
	size_t next = 0;
	for (const std::array<uint8_t, 3>& permutation : permutations) {
		for (uint32_t signs = 0; signs < 8; ++signs) {
			LatticeOrientation orientation;
			orientation.source = permutation;
			orientation.flip = {(signs & 1u) != 0, (signs & 2u) != 0, (signs & 4u) != 0};
			out[next++] = orientation;
		}
	}
	return out;
}

[[nodiscard]]
inline auto toMatrix(const LatticeOrientation& orientation) -> glm::mat3 {
	glm::mat3 matrix(0.0f);
	for (int row = 0; row < 3; ++row) {
		matrix[orientation.source[row]][row] = orientation.flip[row] ? -1.0f : 1.0f;    // glm indexes [column][row]
	}
	return matrix;
}

struct LatticePlacement {
	LatticeOrientation orientation;

	glm::ivec3 offset {0};

	[[nodiscard]]
	auto operator==(const LatticePlacement&) const noexcept -> bool = default;
};

/// @brief A flipped axis lands cell [v, v + 1] on [-v - 1, -v] hence the -1
[[nodiscard]]
inline auto placeVoxel(const LatticePlacement& placement, glm::ivec3 voxel) noexcept -> glm::ivec3 {
	glm::ivec3 out;
	for (int axis = 0; axis < 3; ++axis) {
		const int32_t along = voxel[placement.orientation.source[axis]];
		out[axis] = placement.offset[axis] + (placement.orientation.flip[axis] ? -along - 1 : along);
	}
	return out;
}

/// @brief Matrix entries within @p tolerance of 0 or +/-1 and translation within a millimetre of a voxel boundary
[[nodiscard]]
inline auto placementFromTransform(const glm::mat4& transform, float voxel_size = k_voxel_size, float tolerance = 1e-4f)
    -> std::optional<LatticePlacement> {
	LatticePlacement out;
	std::array<bool, 3> used {false, false, false};

	for (int row = 0; row < 3; ++row) {
		int found = -1;
		for (int column = 0; column < 3; ++column) {
			const float value = transform[column][row];
			if (std::abs(std::abs(value) - 1.0f) <= tolerance) {
				if (found != -1) {
					return std::nullopt;
				}
				found = column;
				out.orientation.flip[row] = value < 0.0f;
			} else if (std::abs(value) > tolerance) {
				return std::nullopt;
			}
		}
		if (found == -1 || used[found]) {
			return std::nullopt;
		}
		used[found] = true;
		out.orientation.source[row] = static_cast<uint8_t>(found);
	}

	if (std::abs(transform[0][3]) > tolerance || std::abs(transform[1][3]) > tolerance || std::abs(transform[2][3]) > tolerance ||
	    std::abs(transform[3][3] - 1.0f) > tolerance) {
		return std::nullopt;
	}

	const float slack = 0.001f / voxel_size;
	for (int axis = 0; axis < 3; ++axis) {
		const float voxels = transform[3][axis] / voxel_size;
		const float nearest = std::round(voxels);
		if (std::abs(voxels - nearest) > slack) {
			return std::nullopt;
		}
		out.offset[axis] = static_cast<int32_t>(nearest);
	}
	return out;
}

/// @brief Index 0 always maps to 0
using PaletteRemapTable = std::array<uint8_t, k_palette_size>;

[[nodiscard]]
inline auto identityRemap() -> PaletteRemapTable {
	PaletteRemapTable table {};
	for (uint32_t i = 0; i < k_palette_size; ++i) {
		table[i] = static_cast<uint8_t>(i);
	}
	return table;
}

struct PaletteRemap {
	PaletteRemapTable table {};

	std::vector<uint8_t> unmatched;
};

[[nodiscard]]
inline auto buildRemap(const Palette& piece, const Palette& master) -> PaletteRemap {
	PaletteRemap out;
	for (uint32_t i = 1; i < k_palette_size; ++i) {
		const PaletteEntry& entry = piece.entries[i];
		if (entry == PaletteEntry {}) {
			continue;
		}

		bool matched = false;
		if (entry.emissive == 0 || piece.max_emissive == master.max_emissive) {
			for (uint32_t j = 1; j < k_palette_size; ++j) {
				if (master.entries[j] == entry) {
					out.table[i] = static_cast<uint8_t>(j);
					matched = true;
					break;
				}
			}
		}
		if (!matched) {
			out.unmatched.push_back(static_cast<uint8_t>(i));
		}
	}
	return out;
}

struct StampResult {
	uint32_t voxels_written = 0;

	uint32_t voxels_clipped = 0;

	/// Skipped since stamping them as empty would erase the target
	uint32_t voxels_unmapped = 0;

	bool pool_exhausted = false;
};

/// @brief @p target and @p piece must be different volumes
[[nodiscard]]
TOAST_API auto stamp(Volume& target, const Volume& piece, const LatticePlacement& placement, const PaletteRemapTable& remap)
    -> StampResult;

}
