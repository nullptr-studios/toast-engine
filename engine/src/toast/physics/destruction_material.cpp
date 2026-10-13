#include "destruction_material.hpp"

#include "material_defaults.hpp"

#include <optional>
#include <string>
#include <toast/log.hpp>

assets::DestructionMaterial::DestructionMaterial(const toml::table& table, Handle<Schema> schema)
    : Data(table, std::move(schema), Data::keep_all_keys) {
	readFields();
}

void assets::DestructionMaterial::onReloaded() {
	readFields();
}

void assets::DestructionMaterial::readFields() {
	m_density = physics::material_defaults::k_density;
	m_toughness = physics::material_defaults::k_toughness;
	m_hardness = physics::material_defaults::k_hardness;
	m_structural_strength = physics::material_defaults::k_structural_strength;
	m_destructible = true;
	m_flammable = false;
	m_burn_rate = 0.0f;
	const auto& d = static_cast<const DataValue&>(m_root);
	if (d.contains("density")) {
		m_density = d["density"].as<float>();
	}
	if (d.contains("toughness")) {
		m_toughness = d["toughness"].as<float>();
	}
	if (d.contains("hardness")) {
		const std::string name = d["hardness"].as<std::string>();
		if (const std::optional<physics::Hardness> hardness = physics::hardnessFromName(name)) {
			m_hardness = *hardness;
		} else {
			TOAST_WARN("DestructionMaterial", "Unknown hardness '{}', using '{}'", name, physics::hardnessName(m_hardness));
		}
	}
	if (d.contains("structural_strength")) {
		m_structural_strength = d["structural_strength"].as<float>();
	}
	if (d.contains("destructible")) {
		m_destructible = d["destructible"].as<bool>();
	}
	if (d.contains("flammable")) {
		m_flammable = d["flammable"].as<bool>();
	}
	if (d.contains("burn_rate")) {
		m_burn_rate = d["burn_rate"].as<float>();
	}
}

auto assets::DestructionMaterial::get() const -> toml::table {
	return m_root.asTomlTable();
}

auto assets::DestructionMaterial::density() const noexcept -> float {
	return m_density;
}

auto assets::DestructionMaterial::toughness() const noexcept -> float {
	return m_toughness;
}

auto assets::DestructionMaterial::hardness() const noexcept -> physics::Hardness {
	return m_hardness;
}

auto assets::DestructionMaterial::structuralStrength() const noexcept -> float {
	return m_structural_strength;
}

auto assets::DestructionMaterial::destructible() const noexcept -> bool {
	return m_destructible;
}

auto assets::DestructionMaterial::flammable() const noexcept -> bool {
	return m_flammable;
}

auto assets::DestructionMaterial::burnRate() const noexcept -> float {
	return m_burn_rate;
}
