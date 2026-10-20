/**
 * @file fracture_profile.hpp
 * @author Xein
 * @date 18 Oct 2026
 * @brief How a material breaks: how the energy of a hit is spent and how coarse its cracks are
 */

#pragma once

#include "material_defaults.hpp"

#include <algorithm>
#include <cstdint>

namespace physics {

struct FractureValues {
	float debris_share = material_defaults::k_debris_share;
	float fragment_share = material_defaults::k_fragment_share;

	float fragment_threshold = material_defaults::k_fragment_threshold;
	uint32_t fragment_max_parts = material_defaults::k_fragment_max_parts;

	float shatter_threshold = material_defaults::k_shatter_threshold;
	float shatter_cell_size = material_defaults::k_shatter_cell_size;
	float shatter_crack_cost = material_defaults::k_shatter_crack_cost;
	float shatter_release = material_defaults::k_shatter_release;
	float shatter_stretch = material_defaults::k_shatter_stretch;
	float shatter_crumble = material_defaults::k_shatter_crumble;

	float edge_noise = material_defaults::k_edge_noise;
	float edge_lobes = material_defaults::k_edge_lobes;
	float edge_noise_scale = material_defaults::k_edge_noise_scale;
};

namespace detail {

[[nodiscard]]
constexpr auto quantize(float value, float steps_per_unit, float max_steps) -> float {
	if (not(value > 0.0f)) {
		return 0.0f;
	}
	const float steps = (value * steps_per_unit) + 0.5f;
	return steps >= max_steps ? max_steps : static_cast<float>(static_cast<uint32_t>(steps));
}

}

struct PackedFractureProfile {
	uint16_t fragment_threshold = 0;
	uint16_t shatter_threshold = 0;
	uint8_t debris_share = 0;
	uint8_t fragment_share = 0;
	uint8_t edge_noise = 0;
	uint8_t edge_lobes = 0;
	uint8_t shatter_release = 0;
	uint8_t shatter_crumble = 0;
	uint8_t edge_noise_scale = 0;
	uint8_t shatter_crack_cost = 0;
	uint8_t shatter_stretch = 0;
	uint8_t shatter_cell_size = 0;
	uint8_t fragment_max_parts = 0;

	[[nodiscard]]
	static constexpr auto pack(const FractureValues& v) -> PackedFractureProfile {
		return PackedFractureProfile {
		  .fragment_threshold = static_cast<uint16_t>(detail::quantize(v.fragment_threshold, 1000.0f, 65535.0f)),
		  .shatter_threshold = static_cast<uint16_t>(detail::quantize(v.shatter_threshold, 1000.0f, 65535.0f)),
		  .debris_share = static_cast<uint8_t>(detail::quantize(v.debris_share, 255.0f, 255.0f)),
		  .fragment_share = static_cast<uint8_t>(detail::quantize(v.fragment_share, 255.0f, 255.0f)),
		  .edge_noise = static_cast<uint8_t>(detail::quantize(v.edge_noise, 255.0f, 255.0f)),
		  .edge_lobes = static_cast<uint8_t>(detail::quantize(v.edge_lobes, 255.0f, 255.0f)),
		  .shatter_release = static_cast<uint8_t>(detail::quantize(v.shatter_release, 255.0f, 255.0f)),
		  .shatter_crumble = static_cast<uint8_t>(detail::quantize(v.shatter_crumble, 255.0f, 255.0f)),
		  .edge_noise_scale = static_cast<uint8_t>(detail::quantize(v.edge_noise_scale, 4.0f, 255.0f)),
		  .shatter_crack_cost = static_cast<uint8_t>(detail::quantize(v.shatter_crack_cost, 64.0f, 255.0f)),
		  .shatter_stretch = static_cast<uint8_t>(detail::quantize(v.shatter_stretch, 16.0f, 255.0f)),
		  .shatter_cell_size = static_cast<uint8_t>(detail::quantize(v.shatter_cell_size, 100.0f, 255.0f)),
		  .fragment_max_parts = static_cast<uint8_t>(std::min<uint32_t>(v.fragment_max_parts, 255u)),
		};
	}

	[[nodiscard]]
	constexpr auto unpack() const -> FractureValues {
		return FractureValues {
		  .debris_share = static_cast<float>(debris_share) / 255.0f,
		  .fragment_share = static_cast<float>(fragment_share) / 255.0f,
		  .fragment_threshold = static_cast<float>(fragment_threshold) / 1000.0f,
		  .fragment_max_parts = fragment_max_parts,
		  .shatter_threshold = static_cast<float>(shatter_threshold) / 1000.0f,
		  .shatter_cell_size = static_cast<float>(shatter_cell_size) / 100.0f,
		  .shatter_crack_cost = static_cast<float>(shatter_crack_cost) / 64.0f,
		  .shatter_release = static_cast<float>(shatter_release) / 255.0f,
		  .shatter_stretch = static_cast<float>(shatter_stretch) / 16.0f,
		  .shatter_crumble = static_cast<float>(shatter_crumble) / 255.0f,
		  .edge_noise = static_cast<float>(edge_noise) / 255.0f,
		  .edge_lobes = static_cast<float>(edge_lobes) / 255.0f,
		  .edge_noise_scale = static_cast<float>(edge_noise_scale) / 4.0f,
		};
	}
};

namespace material_defaults {
inline constexpr PackedFractureProfile k_fracture_profile = PackedFractureProfile::pack(FractureValues {});
}

}
