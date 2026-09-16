#include "physics_material.hpp"

namespace physics { }

assets::PhysicsMaterial::PhysicsMaterial(const toml::table& table, Handle<Schema> schema)
    : Data(table, std::move(schema), Data::keep_all_keys) {
	const auto& d = static_cast<const DataValue&>(m_root);
	if (d.contains("restitution")) {
		m_restitution = d["restitution"].as<float>();
	}
	if (d.contains("static_friction")) {
		m_static_friction = d["static_friction"].as<float>();
	}
	if (d.contains("dynamic_friction")) {
		m_dynamic_friction = d["dynamic_friction"].as<float>();
	}
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
