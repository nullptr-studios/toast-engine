/**
 * @file palette.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "voxel_constants.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace voxel {

inline constexpr uint32_t k_max_physical_materials = 32;

inline constexpr uint8_t k_default_material = 0;

inline constexpr uint8_t k_entry_transparent = 1u << 0;

/// @brief 16 bytes and the offsets are a shader contract
struct PaletteEntry {
	uint8_t albedo_r = 0;
	uint8_t albedo_g = 0;
	uint8_t albedo_b = 0;

	/// 0-255 for 0-1
	uint8_t roughness = 0;
	uint8_t metallic = 0;

	/// Separate from metallic since a wet non metal reflects and a dirty metal barely does
	uint8_t reflectivity = 0;

	/// Scaled by Palette::max_emissive
	uint8_t emissive = 0;

	uint8_t material = k_default_material;

	uint8_t flags = 0;

	/// 0 burns away and only read for flammable materials
	uint8_t transforms_to = 0;

	uint8_t alpha = 255;

	std::array<uint8_t, 5> reserved {};

	[[nodiscard]]
	constexpr auto operator==(const PaletteEntry&) const noexcept -> bool = default;
};

static_assert(sizeof(PaletteEntry) == 16, "a palette entry must be sixteen bytes - the shader indexes it that way");

/// @brief Entry 0 is the empty voxel and must stay all zero
struct Palette {
	std::array<PaletteEntry, k_palette_size> entries {};

	float max_emissive = 1.0f;
};

[[nodiscard]]
inline auto emissiveIntensity(const Palette& palette, uint8_t index) noexcept -> float {
	return static_cast<float>(palette.entries[index].emissive) / 255.0f * palette.max_emissive;
}

inline constexpr uint8_t k_material_indestructible = 1u << 0;

/// @brief "Passable" so the zero default is solid
inline constexpr uint8_t k_material_passable = 1u << 1;

inline constexpr uint8_t k_material_flammable = 1u << 2;

struct PhysicalMaterial {
	/// kg/m³ never zero and integer so MassMoments stays exact
	uint16_t density = 0;

	uint8_t flags = 0;

	/// Uncalibrated joules so only ordering matters
	float toughness = 0.0f;

	float structural_strength = 0.0f;

	float shatter_radius = 0.0f;

	float static_friction = 0.6f;
	float dynamic_friction = 0.5f;
	float restitution = 0.1f;

	float ignition_energy = 0.0f;
	float burn_rate = 0.0f;
	float fuel = 0.0f;

	[[nodiscard]]
	constexpr auto isIndestructible() const noexcept -> bool {
		return (flags & k_material_indestructible) != 0;
	}

	[[nodiscard]]
	constexpr auto collides() const noexcept -> bool {
		return (flags & k_material_passable) == 0;
	}

	[[nodiscard]]
	constexpr auto isFlammable() const noexcept -> bool {
		return (flags & k_material_flammable) != 0;
	}
};

static_assert(sizeof(PhysicalMaterial) <= 64, "a physical material must fit in one cache line");

struct MaterialLibrary {
	std::vector<PhysicalMaterial> materials;
};

/// @brief The library must not be empty
[[nodiscard]]
inline auto resolveMaterialIndex(const Palette& palette, const MaterialLibrary& library, uint8_t palette_index) noexcept
    -> uint32_t {
	const uint8_t material = palette.entries[palette_index].material;
	return material < library.materials.size() ? material : k_default_material;
}

[[nodiscard]]
inline auto massPerVoxel(const PhysicalMaterial& material, float voxel_size = k_voxel_size) noexcept -> float {
	return static_cast<float>(material.density) * voxel_size * voxel_size * voxel_size;
}

enum class TableIssue : uint8_t {
	library_empty,
	/// index is the count
	library_too_large,
	zero_density,
	out_of_range,
	indestructible_with_zero_toughness,
	reserved_slot_used,
	unknown_material,
};

/// @brief index is a material index a palette index or a count
struct TableProblem {
	TableIssue issue = TableIssue::library_empty;
	uint32_t index = 0;

	[[nodiscard]]
	constexpr auto operator==(const TableProblem&) const noexcept -> bool = default;
};

[[nodiscard]]
inline auto validateLibrary(const MaterialLibrary& library) -> std::vector<TableProblem> {
	std::vector<TableProblem> out;
	if (library.materials.empty()) {
		out.push_back({TableIssue::library_empty, 0});
		return out;
	}
	if (library.materials.size() > k_max_physical_materials) {
		out.push_back({TableIssue::library_too_large, static_cast<uint32_t>(library.materials.size())});
	}

	// !(x >= 0) also catches NaN
	const auto negative = [](float value) { return !(value >= 0.0f); };

	for (uint32_t i = 0; i < library.materials.size(); ++i) {
		const PhysicalMaterial& m = library.materials[i];
		if (m.density == 0) {
			out.push_back({TableIssue::zero_density, i});
		}
		if (negative(m.toughness) || negative(m.structural_strength) || negative(m.shatter_radius) || negative(m.static_friction) ||
		    negative(m.dynamic_friction) || negative(m.restitution) || m.restitution > 1.0f || negative(m.ignition_energy) ||
		    negative(m.burn_rate) || negative(m.fuel)) {
			out.push_back({TableIssue::out_of_range, i});
		}
		if (m.isIndestructible() && m.toughness == 0.0f) {
			out.push_back({TableIssue::indestructible_with_zero_toughness, i});
		}
	}
	return out;
}

[[nodiscard]]
inline auto validatePalette(const Palette& palette, const MaterialLibrary& library) -> std::vector<TableProblem> {
	std::vector<TableProblem> out;
	if (!(palette.entries[k_empty_palette_index] == PaletteEntry {})) {
		out.push_back({TableIssue::reserved_slot_used, k_empty_palette_index});
	}
	if (library.materials.empty()) {
		out.push_back({TableIssue::library_empty, 0});
		return out;
	}
	for (uint32_t i = 1; i < k_palette_size; ++i) {
		if (palette.entries[i].material >= library.materials.size()) {
			out.push_back({TableIssue::unknown_material, i});
		}
	}
	return out;
}

}
