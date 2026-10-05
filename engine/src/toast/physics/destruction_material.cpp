#include "destruction_material.hpp"

assets::DestructionMaterial::DestructionMaterial(const toml::table& table, Handle<Schema> schema)
    : Data(table, std::move(schema), Data::keep_all_keys) {
	readFields();
}

void assets::DestructionMaterial::onReloaded() {
	readFields();
}

void assets::DestructionMaterial::readFields() {
	m_density = 1000.0f;
	m_toughness = 1000.0f;
	m_structural_strength = 1000.0f;
	m_shatter_radius = 0.5f;
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
	if (d.contains("structural_strength")) {
		m_structural_strength = d["structural_strength"].as<float>();
	}
	if (d.contains("shatter_radius")) {
		m_shatter_radius = d["shatter_radius"].as<float>();
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

auto assets::DestructionMaterial::structuralStrength() const noexcept -> float {
	return m_structural_strength;
}

auto assets::DestructionMaterial::shatterRadius() const noexcept -> float {
	return m_shatter_radius;
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
