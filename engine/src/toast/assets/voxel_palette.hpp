/**
 * @file voxel_palette.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "core_types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <toast/voxel/palette.hpp>
#include <vector>

namespace assets {

class PhysicsMaterial;
class DestructionMaterial;

inline constexpr uint32_t k_palette_material_slots = 8;

struct VoxelMaterialSlot {
	std::string name;

	uint64_t physics_uid = 0;
	uint64_t destruction_uid = 0;

	uint64_t impact_sound = 0;
	std::array<uint8_t, 3> dust_colour {128, 128, 128};
	std::string tag;

	[[nodiscard]]
	auto operator==(const VoxelMaterialSlot&) const -> bool = default;
};

using VoxelMaterialSlots = std::array<VoxelMaterialSlot, k_palette_material_slots>;

class TOAST_API VoxelPalette : public Asset, public ISaveable {
public:
	VoxelPalette(voxel::Palette palette, VoxelMaterialSlots slots, std::vector<uint8_t> defaulted);

	/// @brief Throws on values an entry cannot hold and leaves material existence to validatePalette
	[[nodiscard]]
	static auto fromToml(const toml::table& table) -> std::unique_ptr<VoxelPalette>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "voxel_palette";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto palette() const noexcept -> const voxel::Palette& {
		return m_palette;
	}

	[[nodiscard]]
	auto slots() const noexcept -> const VoxelMaterialSlots& {
		return m_slots;
	}

	[[nodiscard]]
	auto materialLibrary() const -> const voxel::MaterialLibrary&;

	/// @brief Round tripped so the warning survives a save
	[[nodiscard]]
	auto defaultedEntries() const noexcept -> const std::vector<uint8_t>& {
		return m_defaulted;
	}

private:
	voxel::Palette m_palette;
	VoxelMaterialSlots m_slots;
	std::vector<uint8_t> m_defaulted;

	mutable voxel::MaterialLibrary m_library;
	mutable std::array<Handle<PhysicsMaterial>, k_palette_material_slots> m_physics;
	mutable std::array<Handle<DestructionMaterial>, k_palette_material_slots> m_destruction;
};

}
