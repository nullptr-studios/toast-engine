/**
 * @file hardness.hpp
 * @author Xein
 * @date 12 Oct 2026
 * @brief Tiers of how hard a material is to break
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace physics {

enum class Hardness : uint8_t {
	soft,
	medium,
	hard,
};

inline constexpr std::array<std::string_view, 3> k_hardness_names {"soft", "medium", "hard"};

inline constexpr Hardness k_hardest = Hardness::hard;

[[nodiscard]]
constexpr auto hardnessName(Hardness hardness) -> std::string_view {
	const auto index = static_cast<size_t>(hardness);
	return index < k_hardness_names.size() ? k_hardness_names[index] : std::string_view {};
}

[[nodiscard]]
constexpr auto hardnessFromName(std::string_view name) -> std::optional<Hardness> {
	for (size_t i = 0; i < k_hardness_names.size(); ++i) {
		if (k_hardness_names[i] == name) {
			return static_cast<Hardness>(i);
		}
	}
	return std::nullopt;
}

[[nodiscard]]
constexpr auto canBreak(Hardness max_hardness, Hardness material) -> bool {
	return static_cast<uint8_t>(material) <= static_cast<uint8_t>(max_hardness);
}

}
