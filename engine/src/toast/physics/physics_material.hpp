/**
 * @file physics_material.hpp
 * @author Xein
 * @date 11 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <toast/assets/data.hpp>

namespace assets {

class TOAST_API PhysicsMaterial : public Data {
public:
	explicit PhysicsMaterial(const toml::table& table, Handle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "physics_material";
	}

	[[nodiscard]]
	auto get() const -> toml::table;
	[[nodiscard]]
	auto restitution() const noexcept -> float;
	[[nodiscard]]
	auto staticFriction() const noexcept -> float;
	[[nodiscard]]
	auto dynamicFriction() const noexcept -> float;

private:
	float m_restitution = 0.0f;
	float m_static_friction = 0.6f;
	float m_dynamic_friction = 0.4f;
};

}
