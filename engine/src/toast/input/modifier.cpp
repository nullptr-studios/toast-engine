#include "modifier.hpp"

#include <toast/log.hpp>

namespace input {

auto IModifier::fromToml(const toml::table& table) -> std::shared_ptr<IModifier> {
	const std::string modifier_name = table["name"].value_or<std::string>("");

	if (modifier_name == "deadzone") {
		return std::make_shared<DeadzoneModifier>(table);
	}
	if (modifier_name == "invert") {
		return std::make_shared<InvertModifier>(table);
	}
	if (modifier_name == "exponential") {
		return std::make_shared<ExponentialModifier>(table);
	}
	if (modifier_name == "scale") {
		return std::make_shared<ScaleModifier>(table);
	}
	if (modifier_name == "scale_by_deltatime") {
		return std::make_shared<ScaleByDeltaTimeModifier>(table);
	}
	if (modifier_name == "smooth") {
		return std::make_shared<SmoothModifier>(table);
	}
	if (modifier_name == "key") {
		return std::make_shared<KeyModifier>(table);
	}

	TOAST_WARN("Input", "Unknown modifier '{}'; ignoring it", modifier_name);
	return nullptr;
}

}
