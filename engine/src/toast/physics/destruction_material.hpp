/**
 * @file destruction_material.hpp
 * @author Xein
 * @date 22 Sep 2026
 *
 * @brief Destruction properties for a voxel material
 */

#pragma once
#include "fracture_pattern.hpp"

#include <toast/assets/data.hpp>
#include <toast/physics/hardness.hpp>

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

	/// @brief kJ/m3 it takes to break the material
	[[nodiscard]]
	auto toughness() const noexcept -> float;

	/// @brief Damage that breaks only softer tiers never breaks this material
	[[nodiscard]]
	auto hardness() const noexcept -> physics::Hardness;

	[[nodiscard]]
	auto fracturePatterh() const noexcept -> physics::FracturePattern;

	/// @brief Pascals
	[[nodiscard]]
	auto structuralStrength() const noexcept -> float;

	/// @brief False means voxels of this material never break, whatever hits them
	[[nodiscard]]
	auto destructible() const noexcept -> bool;

	[[nodiscard]]
	auto flammable() const noexcept -> bool;

	[[nodiscard]]
	auto burnRate() const noexcept -> float;

protected:
	void onReloaded() override;

private:
	void readFields();

	float m_density = 0.0f;
	float m_toughness = 0.0f;
	physics::Hardness m_hardness = physics::Hardness::medium;
	physics::FracturePattern m_fracture_pattern = physics::FracturePattern::brittle;
	float m_structural_strength = 0.0f;
	bool m_destructible = true;
	bool m_flammable = false;
	float m_burn_rate = 0.0f;
};

}
