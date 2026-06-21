#include "action.hpp"

#include "parse_util.hpp"

#include <array>
#include <toast/log.hpp>

namespace input {

namespace {

auto zeroValue(ValueType vt) -> Value {
	switch (vt) {
		case ValueType::axis_0d: return Value {false, ValueType::axis_0d};
		case ValueType::axis_1d: return Value {0.0f, ValueType::axis_1d};
		case ValueType::axis_2d: return Value {glm::vec2 {0.0f}, ValueType::axis_2d};
	}
	return Value {};
}

auto contains(const SignalList& list, ActionEvent event) -> bool {
	for (ActionEvent e : list) {
		if (e == event) {
			return true;
		}
	}
	return false;
}

}

auto Action::fromAsset(assets::AssetHandle<assets::Action> handle) -> std::unique_ptr<Action> {
	if (!handle.hasValue()) {
		return nullptr;
	}

	auto action = std::make_unique<Action>();
	action->m_handle = handle;
	action->m_uid = handle.uid();

	const assets::Action& asset = *handle;
	action->m_name = asset.name();
	action->m_function_name = asset.functionName();
	action->m_accumulation = asset.accumulation();

	switch (asset.valueType()) {
		case assets::ActionValueType::action_0d: action->m_value_type = ValueType::axis_0d; break;
		case assets::ActionValueType::action_1d: action->m_value_type = ValueType::axis_1d; break;
		case assets::ActionValueType::action_2d: action->m_value_type = ValueType::axis_2d; break;
	}
	action->m_value = zeroValue(action->m_value_type);

	const toml::table& table = asset.get();

	if (const auto* binds = table["bind"].as_array()) {
		for (const auto& node : *binds) {
			if (const auto* bind_table = node.as_table()) {
				action->m_binds.push_back(Bind::fromToml(*bind_table, action->m_value_type));
			}
		}
	}

	if (const auto* modifiers = table["modifier"].as_array()) {
		for (const auto& node : *modifiers) {
			if (const auto* modifier_table = node.as_table()) {
				if (auto modifier = IModifier::fromToml(*modifier_table)) {
					action->m_modifiers.push_back(std::move(modifier));
				}
			}
		}
	}

	TOAST_TRACE(
	    "Input", "Built action '{}' (fn '{}') with {} bind(s)", action->m_name, action->m_function_name, action->m_binds.size()
	);
	return action;
}

auto Action::evaluate(const Sampler& sampler, const EvalContext& ctx) -> SignalList {
	SignalList fired;

	glm::vec2 accumulated {0.0f};
	float best_magnitude = -1.0f;
	int active_count = 0;
	bool any_active = false;

	for (auto& bind : m_binds) {
		const InputSample sample = sampler(bind.keycode());
		Bind::Result result = bind.evaluate(sample, ctx);

		// Collapse repeated events so each kind is reported at most once per frame
		for (ActionEvent event : result.signals) {
			if (!contains(fired, event)) {
				fired.emit(event);
			}
		}

		if (!result.active) {
			continue;
		}

		any_active = true;
		++active_count;

		if (m_accumulation == assets::AccumulationType::average) {
			accumulated += result.value.data;
		} else if (result.value.magnitude() > best_magnitude) {
			best_magnitude = result.value.magnitude();
			accumulated = result.value.data;
		}

		// Remember the strongest contributor's device
		if (result.value.magnitude() >= best_magnitude) {
			m_device = bind.keycode().device;
		}
	}

	if (any_active && m_accumulation == assets::AccumulationType::average && active_count > 0) {
		accumulated /= static_cast<float>(active_count);
	}

	m_value = any_active ? Value {accumulated, m_value_type} : zeroValue(m_value_type);

	for (auto& modifier : m_modifiers) {
		modifier->apply(m_value, ctx);
	}

	m_modifier_keys = ctx.mods;
	if (!any_active) {
		m_device = Device::none;
	}

	updateTiming(fired, any_active, ctx.delta);

	return fired;
}

void Action::updateTiming(const SignalList& fired, bool active, float delta) {
	if (contains(fired, ActionEvent::try_)) {
		m_time_since_try = 0.0f;
		m_was_trying = true;
	}
	if (contains(fired, ActionEvent::start)) {
		m_time_since_start = 0.0f;
		m_was_trying = false;
	}
	if (contains(fired, ActionEvent::release) || contains(fired, ActionEvent::cancelled)) {
		m_was_trying = false;
	}

	if (active) {
		m_time_since_start += delta;
	} else {
		m_time_since_start = 0.0f;
	}

	if (m_was_trying) {
		m_time_since_try += delta;
	}

	m_was_active = active;
}

auto Action::uid() const noexcept -> toast::UID {
	return m_uid;
}

auto Action::name() const noexcept -> std::string_view {
	return m_name;
}

auto Action::functionName() const noexcept -> std::string_view {
	return m_function_name;
}

auto Action::valueType() const noexcept -> ValueType {
	return m_value_type;
}

auto Action::value() const noexcept -> const Value& {
	return m_value;
}

auto Action::modifiers() const noexcept -> ModifierKey {
	return m_modifier_keys;
}

auto Action::device() const noexcept -> Device {
	return m_device;
}

auto Action::timeSinceStart() const noexcept -> float {
	return m_time_since_start;
}

auto Action::timeSinceTry() const noexcept -> float {
	return m_time_since_try;
}

auto Action::remainingCountdown() const noexcept -> float {
	return m_remaining_countdown;
}

}
