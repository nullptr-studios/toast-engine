#include "destruction_material.hpp"

#include "material_defaults.hpp"

#include <algorithm>
#include <cstdint>
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
	m_fracture_pattern = physics::material_defaults::k_fracture_pattern;
	m_fracture_values = physics::FractureValues {};
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
	if (d.contains("fracture_pattern")) {
		const std::string name = d["fracture_pattern"].as<std::string>();
		if (std::optional<physics::FracturePattern> fracture_pattern = physics::fracturePatternFromName(name)) {
			m_fracture_pattern = *fracture_pattern;
		} else {
			TOAST_WARN(
			    "DestructionMaterial",
			    "Unknown fracture pattern '{}', using '{}'",
			    name,
			    physics::fracturePatternName(m_fracture_pattern)
			);
		}
	}
	const auto group = [&d](const char* name) -> const DataValue* {
		return d.contains(name) && d[name].isObject() ? &d[name] : nullptr;
	};
	const auto read_float = [](const DataValue* table, const char* key, float& value) {
		if (table != nullptr && table->contains(key)) {
			value = (*table)[key].as<float>();
		}
	};
	const auto read_count = [](const DataValue* table, const char* key, uint32_t& value) {
		if (table != nullptr && table->contains(key)) {
			value = static_cast<uint32_t>(std::max<int64_t>((*table)[key].as<int64_t>(), 0));
		}
	};
	physics::FractureValues& values = m_fracture_values;
	const DataValue* energy = group("energy");
	read_float(energy, "debris_share", values.debris_share);
	read_float(energy, "fragment_share", values.fragment_share);
	const DataValue* fragment = group("fragment");
	read_float(fragment, "threshold", values.fragment_threshold);
	read_count(fragment, "max_parts", values.fragment_max_parts);
	const DataValue* shatter = group("shatter");
	read_float(shatter, "threshold", values.shatter_threshold);
	read_float(shatter, "cell_size", values.shatter_cell_size);
	read_float(shatter, "crack_cost", values.shatter_crack_cost);
	read_float(shatter, "release", values.shatter_release);
	read_float(shatter, "stretch", values.shatter_stretch);
	read_float(shatter, "crumble", values.shatter_crumble);
	const DataValue* edge = group("edge");
	read_float(edge, "noise", values.edge_noise);
	read_float(edge, "lobes", values.edge_lobes);
	read_float(edge, "noise_scale", values.edge_noise_scale);
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

auto assets::DestructionMaterial::fracturePatterh() const noexcept -> physics::FracturePattern {
	return m_fracture_pattern;
}

auto assets::DestructionMaterial::fractureProfile() const noexcept -> physics::PackedFractureProfile {
	return physics::PackedFractureProfile::pack(m_fracture_values);
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
