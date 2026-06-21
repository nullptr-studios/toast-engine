/**
 * @file bind.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief A bind maps one keycode to a set of triggers and per-bind modifiers
 */

#pragma once

#include "keycodes.hpp"
#include "modifier.hpp"
#include "trigger.hpp"
#include "value.hpp"

#include <memory>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace input {

/**
 * @brief Binds a single keycode to its triggers and per-bind modifiers
 */
class TOAST_API Bind {
public:
	Bind() = default;

	/**
	 * @brief Outcome of evaluating a bind for one frame
	 */
	struct Result {
		bool active = false;
		Value value;
		SignalList signals;
	};

	/**
	 * @brief Builds a bind from a [[bind]] TOML table
	 * @param table The bind table holding keycode, triggers and modifiers
	 * @param vt The value domain of the owning action
	 * @return The constructed bind
	 */
	static auto fromToml(const toml::table& table, ValueType vt) -> Bind;

	/**
	 * @brief Evaluates every trigger and applies per-bind modifiers
	 * @param sample The raw input for this bind's keycode this frame
	 * @param ctx Frame context
	 * @return The bind's contribution and the events its triggers fired
	 */
	auto evaluate(const InputSample& sample, const EvalContext& ctx) -> Result;

	/// @return The parsed keycode this bind reads
	[[nodiscard]]
	auto keycode() const noexcept -> const KeyCode& {
		return m_keycode;
	}

	/// @return The original keycode string
	[[nodiscard]]
	auto keycodeString() const noexcept -> std::string_view {
		return m_keycode_string;
	}

private:
	std::string m_keycode_string;
	KeyCode m_keycode;
	std::vector<std::shared_ptr<ITrigger>> m_triggers;
	std::vector<std::shared_ptr<IModifier>> m_modifiers;
};

}
