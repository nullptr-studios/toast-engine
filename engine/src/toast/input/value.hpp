/**
 * @file value.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Value type shared by triggers, modifiers and actionsg
 */

#pragma once

#include "keycodes.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace input {

/**
 * @brief Value domain of an action or an input sample
 */
enum class ValueType : uint8_t {
	axis_0d,    ///< boolean
	axis_1d,    ///< float
	axis_2d,    ///< vector
};

/**
 * @brief A tagged value that may represent a bool, a float or a glm::vec2
 */
struct Value {
	glm::vec2 data {0.0f, 0.0f};
	ValueType type = ValueType::axis_0d;

	Value() = default;

	Value(bool v, ValueType t = ValueType::axis_0d) : data(v ? 1.0f : 0.0f, 0.0f), type(t) { }

	Value(float v, ValueType t = ValueType::axis_1d) : data(v, 0.0f), type(t) { }

	Value(glm::vec2 v, ValueType t = ValueType::axis_2d) : data(v), type(t) { }

	/// @tparam T Get value as bool, float or glm::vec2
	template<typename T>
	[[nodiscard]]
	auto as() const noexcept -> T;

	/// @return Absolute magnitude used to compare binds under Highest accumulation
	[[nodiscard]]
	auto magnitude() const noexcept -> float {
		if (type == ValueType::axis_2d) {
			return glm::length(data);
		}
		return std::abs(data.x);
	}
};

template<>
inline auto Value::as() const noexcept -> bool {
	return data.x != 0.0f;
}

template<>
inline auto Value::as() const noexcept -> float {
	return data.x;
}

template<>
inline auto Value::as() const noexcept -> glm::vec2 {
	return data;
}

/**
 * @brief Discrete event an action can emit, carried alongside the action in an InputEvent
 */
enum class ActionEvent : uint8_t {
	start,        ///< the input went from off to on
	hold,         ///< the input is held; sent continuously or on a pulse
	release,      ///< the input went from on to off
	try_,         ///< the input was pressed but a countdown must elapse before it starts
	countdown,    ///< equivalent of hold while the start countdown is running
	cancelled,    ///< the input was released before the countdown finished
};

/**
 * @brief Fixed-capacity buffer of discrete events fired in a single frame
 *
 * Avoids per-frame heap allocation in the hot evaluation path
 */
struct SignalList {
	std::array<ActionEvent, 8> items {};
	uint8_t count = 0;

	/// @brief Appends an event, ignoring overflow past the capacity
	void emit(ActionEvent event) noexcept {
		if (count < items.size()) {
			items[count++] = event;
		}
	}

	/// @brief Appends every event from another list
	void merge(const SignalList& other) noexcept {
		for (uint8_t i = 0; i < other.count; ++i) {
			emit(other.items[i]);
		}
	}

	[[nodiscard]]
	auto begin() const noexcept {
		return items.begin();
	}

	[[nodiscard]]
	auto end() const noexcept {
		return items.begin() + count;
	}

	[[nodiscard]]
	auto empty() const noexcept -> bool {
		return count == 0;
	}
};

/**
 * @brief One frame of raw input for a single keycode
 *
 * Buttons, axes and scroll fill @ref scalar; sticks, the touchpad and the cursor fill
 * @ref vector. @ref present is false when the bound device is not currently connected
 */
struct InputSample {
	float scalar = 0.0f;
	glm::vec2 vector {0.0f, 0.0f};
	bool is_vector = false;
	bool present = true;
};

/**
 * @brief Per-frame context passed down to triggers and modifiers during evaluation
 */
struct EvalContext {
	float delta = 0.0f;                      ///< frame delta time in seconds; currently always zero, see input system
	ModifierKey mods = ModifierKey::none;    ///< keyboard modifier keys held this frame
	glm::vec2 viewport_size {0.0f, 0.0f};    ///< size of the active viewport in pixels, for cursor NDC conversion
	glm::vec2 viewport_pos {0.0f, 0.0f};     ///< top-left of the active viewport in pixels
};

}
