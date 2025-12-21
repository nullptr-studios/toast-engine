/// @file InputAction.hpp
/// @date 7 May 2025
/// @author Xein
/// @brief Contains the definitions for the action struct

#pragma once
#include "Bind.hpp"
#include "Engine/Core/Log.hpp"
#include "KeyCodes.hpp"

#include <glm/glm.hpp>
#include <numeric>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <string_view>

namespace input {

// Ensuring Actions can only be created from a bool, float or glm::vec2
template<typename T>
concept ActionValues = std::is_same_v<T, bool> || std::is_same_v<T, float> || std::is_same_v<T, glm::vec2>;

template<ActionValues Value>
struct Action {
	enum State : uint8_t {
		Null = 0,
		Started = 1,
		Ongoing = 2,
		Finished = 3
	};

	Action(const Action&) = default;
	Action(Action&&) = default;

	// All the information available to the user
	std::string name;                             //< @brief Name of the action
	Value value;                                  //< @brief Value of the action
	State state = Null;                           //< @brief State of the current action
	Device device = Device::Null;                 //< @brief Device that performed the action
	ModifierKey modifiers = ModifierKey::None;    //< @brief Keyboard modifiers with the key-press
	float elapsedTime = 0;                        //< @brief Time since the action state was set to Started

protected:
	friend class InputSystem;
	friend class Layout;
	Action() = default;
	static auto create(std::string_view name, const sol::object& obj) noexcept -> std::optional<Action<Value>>;

	void CalculateValue() {
		if (m.pressedKeys.empty()) {
			value = static_cast<Value>(0);
			state = Finished;
		}

		value = std::accumulate(m.pressedKeys.begin(), m.pressedKeys.end(), static_cast<Value>(0.0f), [](Value a, const std::pair<int, Value>& pair) {
			return a + pair.second;
		});

		// Clamp the values after the addition so it's always
		if constexpr (std::is_same_v<Value, float>) {
			constexpr float MIN = -1.0f;
			constexpr float MAX = 1.0f;
			value = std::clamp(value, MIN, MAX);
		} else if constexpr (std::is_same_v<Value, glm::vec2>) {
			glm::vec2 min = { -1.0f, -1.0f };
			glm::vec2 max = { 1.0f, 1.0f };
			value = glm::clamp(value, min, max);
		}

		if (state == Finished || state == Null) {
			state = Started;
		} else {
			state = Ongoing;
		}
	}

	/// Checks if this action should happen on the current InputSystem State
	bool CheckState(std::string_view state) {
		if (m.states.empty()) {
			return true;
		}

		bool blocked = false;
		bool has_positive = false;
		bool matched_positive = false;

		for (const std::string& s : m.states) {
			if (s.empty()) {
				continue;
			}

			if (s.starts_with('-')) {
				// if it matches the current state, block
				if (s.substr(1) == state) {
					blocked = true;
					break;
				}
				continue;
			}

			// record presence and match
			has_positive = true;
			if (s == state) {
				matched_positive = true;
			}
		}

		if (blocked) {
			return false;
		}
		// If any positive gates exist, require a positive match, otherwise allow
		return has_positive ? matched_positive : true;
	}

	struct M {
		std::vector<Bind> binds;
		std::vector<std::string> states;
		std::unordered_map<int, Value> pressedKeys;
	} m;
};

using Action0D = Action<bool>;
using Action1D = Action<float>;
using Action2D = Action<glm::vec2>;

template<ActionValues Value>
auto Action<Value>::create(std::string_view name, const sol::object& obj) noexcept -> std::optional<Action<Value>> {
	// check the object is the correct type
	if (!obj.is<sol::table>()) {
		TOAST_ERROR("Action object is not a Lua Table");
		return std::nullopt;
	}
	auto action_lua = obj.as<sol::table>();
	if (action_lua.empty()) {
		TOAST_ERROR("Action is empty");
		return std::nullopt;
	}

	// creating our action
	Action<Value> action;
	action.name = name;

	// States
	auto states_lua = action_lua["states"].get_or_create<sol::table>();
	action.m.states.reserve(states_lua.size());
	for (const auto& [_, s] : states_lua) {
		// check if it's a string beforehand
		if (!s.is<std::string>()) {
			continue;
		}
		std::string state = s.as<std::string>();
		action.m.states.emplace_back(state);
	}

	TOAST_TRACE("Action has {} states", action.m.states.size());

	// Binds
	auto binds_lua = action_lua["binds"].get_or_create<sol::table>();
	action.m.binds.reserve(binds_lua.size());
	for (const auto& [_, a] : binds_lua) {
		auto bind = Bind::create(a);
		if (!bind.has_value()) {
			continue;
		}
		action.m.binds.emplace_back(*bind);
	}

	// If creating a bind was not possible, we return nullopt
	if (binds_lua.empty()) {
		TOAST_ERROR("Action doesn't have binds");
		return std::nullopt;
	}
	TOAST_TRACE("Action has {} binds", action.m.binds.size());

	TOAST_INFO("Created Action \"{}\"", action.name);
	return action;
}

}
