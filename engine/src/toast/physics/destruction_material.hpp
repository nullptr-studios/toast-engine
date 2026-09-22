/**
 * @file destruction_material.hpp
 * @author Xein
 * @date 22 Sep 2026
 *
 * @brief Destruction properties for a voxel material
 */

#pragma once
#include <toast/assets/data.hpp>

namespace assets {

class TOAST_API DestructionMaterial : public Data {
public:
	explicit DestructionMaterial(const toml::table& table, Handle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "destruction_material";
	}

	[[nodiscard]]
	auto get() const -> toml::table;

	/// @brief kg/m3; drives voxel mass through massPerVoxel()
	[[nodiscard]]
	auto density() const noexcept -> float;

	/// @brief Joules of impulse energy a voxel absorbs before it breaks
	[[nodiscard]]
	auto toughness() const noexcept -> float;

	/// @brief Pascals
	[[nodiscard]]
	auto structuralStrength() const noexcept -> float;

	/// @brief Metres of damage radius when a break does happen
	[[nodiscard]]
	auto shatterRadius() const noexcept -> float;

	[[nodiscard]]
	auto flammable() const noexcept -> bool;

	[[nodiscard]]
	auto burnRate() const noexcept -> float;

private:
	float m_density = 1000.0f;
	float m_toughness = 100.0f;
	float m_structural_strength = 0.0f;
	float m_shatter_radius = 0.5f;
	bool m_flammable = false;
	float m_burn_rate = 0.0f;
};

}
