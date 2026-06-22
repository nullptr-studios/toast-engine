#include "bind.hpp"

#include <toast/log.hpp>

namespace input {

auto Bind::fromToml(const toml::table& table, ValueType vt) -> Bind {
	Bind bind;
	bind.m_keycode_string = table["keycode"].value_or<std::string>("");
	bind.m_keycode = parseKeycode(bind.m_keycode_string);

	if (const auto* triggers = table["trigger"].as_array()) {
		for (const auto& node : *triggers) {
			if (const auto* trigger_table = node.as_table()) {
				if (auto trigger = ITrigger::fromToml(*trigger_table, vt)) {
					bind.m_triggers.push_back(std::move(trigger));
				}
			}
		}
	}

	if (const auto* modifiers = table["modifier"].as_array()) {
		for (const auto& node : *modifiers) {
			if (const auto* modifier_table = node.as_table()) {
				if (auto modifier = IModifier::fromToml(*modifier_table)) {
					bind.m_modifiers.push_back(std::move(modifier));
				}
			}
		}
	}

	return bind;
}

auto Bind::evaluate(const InputSample& sample, const EvalContext& ctx) -> Result {
	Result result;

	// Pick the strongest active trigger as this bind's value; merge every trigger's events
	for (auto& trigger : m_triggers) {
		ITrigger::Result tr = trigger->evaluate(sample, ctx);
		result.signals.merge(tr.signals);
		if (tr.active && (!result.active || tr.value.magnitude() > result.value.magnitude())) {
			result.active = true;
			result.value = tr.value;
		}
	}

	if (result.active) {
		for (auto& modifier : m_modifiers) {
			modifier->apply(result.value, ctx);
		}
	}

	return result;
}

}
