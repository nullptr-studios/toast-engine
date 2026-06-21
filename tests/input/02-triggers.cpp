#include "test_registry.hpp"

#include <cassert>
#include <toast/input/trigger.hpp>
#include <toml++/toml.hpp>

namespace {

auto hasSignal(const input::ITrigger::Result& r, input::ActionEvent event) -> bool {
	for (input::ActionEvent e : r.signals) {
		if (e == event) {
			return true;
		}
	}
	return false;
}

auto pressed() -> input::InputSample {
	input::InputSample s;
	s.scalar = 1.0f;
	return s;
}

auto releasedSample() -> input::InputSample {
	return input::InputSample {};
}

}

TOAST_TEST_NAMED("input", "input/02-triggers", test_input_triggers) {
	using namespace input;
	EvalContext ctx;

	{
		toml::table tbl = toml::parse("name = \"start\"\nthreshold = 0.5\nvalue = true\ncountdown = 0\n");
		auto trigger = ITrigger::fromToml(tbl, ValueType::axis_0d);
		assert(trigger != nullptr);

		ITrigger::Result down = trigger->evaluate(pressed(), ctx);
		assert(down.active && down.value.as<bool>());
		assert(hasSignal(down, ActionEvent::start));

		ITrigger::Result hold = trigger->evaluate(pressed(), ctx);
		assert(!hold.active);
		assert(!hasSignal(hold, ActionEvent::start));

		ITrigger::Result up = trigger->evaluate(releasedSample(), ctx);
		assert(!up.active);
	}

	{
		toml::table tbl = toml::parse("name = \"release\"\nthreshold = 0.5\n");
		auto trigger = ITrigger::fromToml(tbl, ValueType::axis_0d);
		assert(trigger != nullptr);

		ITrigger::Result down = trigger->evaluate(pressed(), ctx);
		assert(!hasSignal(down, ActionEvent::release));

		ITrigger::Result up = trigger->evaluate(releasedSample(), ctx);
		assert(hasSignal(up, ActionEvent::release));
	}

	{
		toml::table tbl = toml::parse("name = \"hold\"\nthreshold_start = 0.5\nthreshold_release = 0.5\nvalue = true\n");
		auto trigger = ITrigger::fromToml(tbl, ValueType::axis_0d);
		assert(trigger != nullptr);

		ITrigger::Result f1 = trigger->evaluate(pressed(), ctx);
		assert(f1.active && hasSignal(f1, ActionEvent::hold));

		ITrigger::Result f2 = trigger->evaluate(pressed(), ctx);
		assert(f2.active && hasSignal(f2, ActionEvent::hold));

		ITrigger::Result f3 = trigger->evaluate(releasedSample(), ctx);
		assert(!f3.active);
	}
}
