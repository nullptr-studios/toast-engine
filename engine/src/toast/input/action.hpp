/**
 * @file action.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Maps a group of binds to a function and a value
 */

#pragma once

#include "bind.hpp"
#include "modifier.hpp"
#include "value.hpp"

#include <toast/assets/input_action.hpp>
#include <toast/assets/types.hpp>
#include <toast/uid.hpp>

namespace input {

class TOAST_API Action {
public:
	Action() = default;

	/// @brief Supplies the current raw input for a keycode; provided by the input system
	using Sampler = std::function<InputSample(const KeyCode&)>;

	/**
	 * @brief Builds a runtime action from its asset
	 * @param handle Handle to the loaded asset::Action; kept alive for the action's lifetime
	 * @return A new action, or nullptr when the handle does not resolve
	 */
	static auto fromAsset(assets::AssetHandle<assets::Action> handle) -> std::unique_ptr<Action>;

	/**
	 * @brief Evaluates every bind, accumulates the value and updates timing state
	 * @param sampler Callback returning the raw input for a given keycode
	 * @param ctx Frame context
	 * @return The distinct discrete events fired this frame, each appearing at most once
	 */
	auto evaluate(const Sampler& sampler, const EvalContext& ctx) -> SignalList;

	[[nodiscard]]
	auto uid() const noexcept -> toast::UID;                   ///< @returns The UID of the backing asset::Action

	[[nodiscard]]
	auto name() const noexcept -> std::string_view;            ///< @returns The action's display name

	[[nodiscard]]
	auto functionName() const noexcept -> std::string_view;    ///< @returns The reflected parent function this action invokes

	[[nodiscard]]
	auto valueType() const noexcept -> ValueType;              ///< @returns The action's value domain

	[[nodiscard]]
	auto value() const noexcept -> const Value&;               ///< @returns The current accumulated value

	[[nodiscard]]
	auto modifiers() const noexcept -> ModifierKey;            ///< @returns The keyboard modifier keys held

	[[nodiscard]]
	auto device() const noexcept -> Device;               ///< @returns The device that most recently contributed to this action

	[[nodiscard]]
	auto timeSinceStart() const noexcept -> float;        ///< @returns Seconds the action has been active

	[[nodiscard]]
	auto timeSinceTry() const noexcept -> float;          ///< @returns Seconds since the start countdown

	[[nodiscard]]
	auto remainingCountdown() const noexcept -> float;    ///< @returns Remaining seconds of the countdown

	[[nodiscard]]
	auto binds() const noexcept -> const std::vector<Bind>& {    ///< @returns The binds that drive this action
		return m_binds;
	}

private:
	void updateTiming(const SignalList& fired, bool active, float delta);

	assets::AssetHandle<assets::Action> m_handle;
	toast::UID m_uid;
	std::string m_name;
	std::string m_function_name;
	ValueType m_value_type = ValueType::axis_0d;
	assets::AccumulationType m_accumulation = assets::AccumulationType::highest;

	std::vector<Bind> m_binds;
	std::vector<std::shared_ptr<IModifier>> m_modifiers;

	Value m_value;
	float m_time_since_start = 0.0f;
	float m_time_since_try = 0.0f;
	float m_remaining_countdown = 0.0f;
	ModifierKey m_modifier_keys = ModifierKey::none;
	Device m_device = Device::none;
	bool m_was_active = false;
	bool m_was_trying = false;
};

}
