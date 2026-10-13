#include "physics_material.hpp"

#include "material_defaults.hpp"

#include <algorithm>

namespace physics { }

assets::PhysicsMaterial::PhysicsMaterial(const toml::table& table, Handle<Schema> schema)
    : Data(table, std::move(schema), Data::keep_all_keys) {
	readFields();
}

void assets::PhysicsMaterial::onReloaded() {
	readFields();
}

void assets::PhysicsMaterial::readFields() {
	m_restitution = physics::material_defaults::k_restitution;
	m_static_friction = physics::material_defaults::k_static_friction;
	m_dynamic_friction = physics::material_defaults::k_dynamic_friction;
	m_rolling_friction = physics::material_defaults::k_rolling_friction;
	m_spin_friction = physics::material_defaults::k_spin_friction;
	const auto& d = static_cast<const DataValue&>(m_root);
	if (d.contains("restitution")) {
		m_restitution = d["restitution"].as<double>();
	}
	if (d.contains("static_friction")) {
		m_static_friction = d["static_friction"].as<double>();
	}
	if (d.contains("dynamic_friction")) {
		m_dynamic_friction = d["dynamic_friction"].as<double>();
	}
	if (d.contains("rolling_friction")) {
		m_rolling_friction = d["rolling_friction"].as<double>();
	}
	if (d.contains("spin_friction")) {
		m_spin_friction = d["spin_friction"].as<double>();
	}
	// sliding should never be higher than static friction
	m_dynamic_friction = std::min(m_dynamic_friction, m_static_friction);
	// a negative resistance would speed the body up
	m_rolling_friction = std::max(m_rolling_friction, 0.0f);
	m_spin_friction = std::max(m_spin_friction, 0.0f);
}

auto assets::PhysicsMaterial::get() const -> toml::table {
	return m_root.asTomlTable();
}

auto assets::PhysicsMaterial::restitution() const noexcept -> float {
	return m_restitution;
}

auto assets::PhysicsMaterial::staticFriction() const noexcept -> float {
	return m_static_friction;
}

auto assets::PhysicsMaterial::dynamicFriction() const noexcept -> float {
	return m_dynamic_friction;
}

auto assets::PhysicsMaterial::rollingFriction() const noexcept -> float {
	return m_rolling_friction;
}

auto assets::PhysicsMaterial::spinFriction() const noexcept -> float {
	return m_spin_friction;
}
