#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/assets/input_action.hpp>
#include <toast/input/action.hpp>
#include <toml++/toml.hpp>

namespace {

auto hasSignal(const input::SignalList& list, input::ActionEvent event) -> bool {
	for (input::ActionEvent e : list) {
		if (e == event) {
			return true;
		}
	}
	return false;
}

}

TOAST_TEST_NAMED("input", "input/05-action", test_input_action) {
	using namespace input;

	const char* toml_str =
	    "name = \"jump\"\n"
	    "function_name = \"onJump\"\n"
	    "type = \"Action0D\"\n"
	    "accumulation = true\n"
	    "[[bind]]\n"
	    "keycode = \"keyboard/space\"\n"
	    "[[bind.trigger]]\n"
	    "name = \"start\"\n"
	    "threshold = 0.5\n"
	    "value = true\n"
	    "countdown = 0\n";

	auto* asset = new assets::Action(toml::parse(toml_str));
	assets::AssetHandle<assets::Action> handle(asset, toast::UID(99));

	auto action = Action::fromAsset(handle);
	assert(action != nullptr);
	assert(action->uid().data() == 99);
	assert(action->functionName() == "onJump");
	assert(action->valueType() == ValueType::axis_0d);
	assert(action->binds().size() == 1);

	EvalContext ctx;
	bool key_down = false;
	auto sampler = [&](const KeyCode& key) {
		InputSample s;
		(void)key;
		s.scalar = key_down ? 1.0f : 0.0f;
		return s;
	};

	SignalList idle = action->evaluate(sampler, ctx);
	assert(idle.empty());
	assert(!action->value().as<bool>());

	key_down = true;
	SignalList press = action->evaluate(sampler, ctx);
	assert(hasSignal(press, ActionEvent::start));
	assert(action->value().as<bool>());

	SignalList held = action->evaluate(sampler, ctx);
	assert(!hasSignal(held, ActionEvent::start));
}
