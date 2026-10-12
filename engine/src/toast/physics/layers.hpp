/**
 * @file layers.hpp
 * @author Xein
 * @date 12 Oct 2026
 * @brief Collision layers
 */

#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <vector>

namespace physics {

inline constexpr int k_layer_count = 32;
inline constexpr uint32_t k_all_layers = 0xFFFF'FFFFu;
inline constexpr size_t k_layer_abbreviation_length = 3;

enum BuiltinLayer : uint8_t {
	default_layer = 0,
	trigger_layer,
	characters_layer,
	player_layer,
	small_debris_layer,
	large_debris_layer,
	projectiles_layer,
};

[[nodiscard]]
constexpr auto layerBit(int layer) -> uint32_t {
	return layer >= 0 && layer < k_layer_count ? 1u << static_cast<uint32_t>(layer) : 0u;
}

struct LayerFilter {
	uint32_t included = layerBit(default_layer);
	uint32_t excluded = 0;
};

/**
 * Puts @p layer in or out of the included mask
 */
constexpr void includeLayer(LayerFilter& filter, int layer, bool value) {
	const uint32_t bit = layerBit(layer);
	filter.included = value ? filter.included | bit : filter.included & ~bit;
	if (value) {
		filter.excluded &= ~bit;
	}
}

constexpr void excludeLayer(LayerFilter& filter, int layer, bool value) {
	const uint32_t bit = layerBit(layer);
	filter.excluded = value ? filter.excluded | bit : filter.excluded & ~bit;
	if (value) {
		filter.included &= ~bit;
	}
}

[[nodiscard]]
constexpr auto layersCollide(LayerFilter a, LayerFilter b) -> bool {
	return (a.included & b.included) != 0 && (a.excluded & b.included) == 0 && (b.excluded & a.included) == 0;
}

/// Mask of the given layer indices
[[nodiscard]]
TOAST_API auto layerMask(const std::vector<int>& layers) -> uint32_t;

/// Index of the layer named @p name
[[nodiscard]]
TOAST_API auto getLayer(std::string_view name) -> int;

[[nodiscard]]
TOAST_API auto layerName(int layer) -> std::string;

[[nodiscard]]
TOAST_API auto layerAbbreviation(int layer) -> std::string;

void registerLayerSettings();

}
