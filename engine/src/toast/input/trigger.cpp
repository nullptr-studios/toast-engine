#include "trigger.hpp"

#include <toast/log.hpp>

namespace input {

auto ITrigger::fromToml(const toml::table& table, ValueType vt) -> std::shared_ptr<ITrigger> {
	const std::string trigger_name = table["name"].value_or<std::string>("start");

	if (trigger_name == "start") {
		return std::make_shared<StartTrigger>(table, vt);
	}
	if (trigger_name == "release") {
		return std::make_shared<ReleaseTrigger>(table, vt);
	}
	if (trigger_name == "hold") {
		return std::make_shared<HoldTrigger>(table, vt);
	}
	if (trigger_name == "pulse") {
		return std::make_shared<PulseTrigger>(table, vt);
	}
	if (trigger_name == "axis1d") {
		return std::make_shared<Axis1DTrigger>(table, vt);
	}
	if (trigger_name == "axis2d") {
		return std::make_shared<Axis2DTrigger>(table, vt);
	}
	if (trigger_name == "cursor") {
		return std::make_shared<CursorTrigger>(table, vt);
	}

	TOAST_WARN("Input", "Unknown trigger '{}'; ignoring it", trigger_name);
	return nullptr;
}

}
