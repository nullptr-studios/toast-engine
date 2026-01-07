/// @file Bind.hpp
/// @date 13 Dec 2025
/// @author Xein

#pragma once
#include "KeyCodes.hpp"

#include <glm/glm.hpp>
#include <sol/sol.hpp>

namespace input {

enum class BindRange : uint8_t {
	Null,
	Full,
	Up,
	Down,
	Left,
	Right,
	x,
	y
};

struct Bind {
	Bind(const Bind&) = default;
	Bind(Bind&&) = default;

	static auto create(const sol::object& obj) -> std::optional<Bind>;

	// This two functions are mainly done to convert a bool bind to a direction
	// This is similar to the composite 1D/composite 2D logic on v1
	// You shouldn't use them for controllers
	float GetFloatValue(BindRange range) const {
		if (range == BindRange::Left || range == BindRange::Down) {
			return -1.0f;
		}
		return 1.0f;
	}

	glm::vec2 GetVec2Value(BindRange range) const {
		switch (range) {
			case BindRange::Full: return { 1.0f, 1.0f };
			case BindRange::y:
			case BindRange::Up: return { 0.0f, 1.0f };
			case BindRange::x:
			case BindRange::Right: return { 1.0f, 0.0f };
			case BindRange::Down: return { 0.0f, -1.0f };
			case BindRange::Left: return { -1.0f, 0.0f };
			default: return { 0.0f, 0.0f };
		}
	}

	Device device = Device::Null;
	std::unordered_map<int, BindRange> keys;
	// std::vector<std::unique_ptr<IModifier>> mods;

private:
	friend class InputSystem;
	Bind() = default;
	static auto create(std::string& key) -> std::optional<Bind>;
	static auto create(const sol::table& table) -> std::optional<Bind>;
	void Insert(const sol::object& key_lua, BindRange range);
};

}
