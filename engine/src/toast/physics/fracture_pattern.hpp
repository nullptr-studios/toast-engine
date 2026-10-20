/**
 * @file fracture_pattern.h
 * @author Xein
 * @date 16 Oct 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace physics {

enum class FracturePattern : uint8_t {
	brittle,     ///< small cells with thin cracks
	granular,    ///< crumbles into debris
	fibrous,     ///< splinters
	ductile,     ///< no shatter
};

inline constexpr std::array<std::string_view, 4> k_fracture_pattern_names {"brittle", "granular", "fibrous", "ductile"};

[[nodiscard]]
constexpr auto fracturePatternName(FracturePattern pattern) -> std::string_view {
	auto index = static_cast<size_t>(pattern);
	return index < k_fracture_pattern_names.size() ? k_fracture_pattern_names[index] : std::string_view {};
}

[[nodiscard]]
constexpr auto fracturePatternFromName(std::string_view name) -> std::optional<FracturePattern> {
	for (size_t i = 0; i < k_fracture_pattern_names.size(); ++i) {
		if (k_fracture_pattern_names[i] == name) {
			return static_cast<FracturePattern>(i);
		}
	}
	return std::nullopt;
}

/// @returns true if the left over energy generates a crack
[[nodiscard]]
constexpr auto shatters(FracturePattern pattern) -> bool {
	return pattern != FracturePattern::ductile;
}

}
