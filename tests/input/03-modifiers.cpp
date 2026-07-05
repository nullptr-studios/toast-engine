#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/input/modifier.hpp>
#include <toml++/toml.hpp>

namespace {

auto approx(float a, float b) -> bool {
	return std::abs(a - b) < 0.0001f;
}

}

TOAST_TEST_NAMED("input", "input/03-modifiers", test_input_modifiers) {
	using namespace input;
	EvalContext ctx;

	{
		toml::table tbl = toml::parse("name = \"deadzone\"\nlower_threshold = 0.2\nupper_threshold = 1.0\n");
		auto modifier = IModifier::fromToml(tbl);
		assert(modifier != nullptr);

		Value below {0.1f, ValueType::axis_1d};
		modifier->apply(below, ctx);
		assert(approx(below.as<float>(), 0.0f));

		Value above {0.6f, ValueType::axis_1d};
		modifier->apply(above, ctx);
		assert(approx(above.as<float>(), 0.5f));    // (0.6 - 0.2) / (1.0 - 0.2)
	}

	{
		toml::table tbl = toml::parse("name = \"invert\"\nx = true\ny = false\n");
		auto modifier = IModifier::fromToml(tbl);
		Value v {glm::vec2 {0.5f, 0.5f}, ValueType::axis_2d};
		modifier->apply(v, ctx);
		assert(approx(v.as<glm::vec2>().x, -0.5f));
		assert(approx(v.as<glm::vec2>().y, 0.5f));
	}

	{
		toml::table tbl = toml::parse("name = \"scale\"\nx = 2.0\ny = 3.0\n");
		auto modifier = IModifier::fromToml(tbl);
		Value v {glm::vec2 {1.0f, 1.0f}, ValueType::axis_2d};
		modifier->apply(v, ctx);
		assert(approx(v.as<glm::vec2>().x, 2.0f));
		assert(approx(v.as<glm::vec2>().y, 3.0f));
	}

	{
		toml::table tbl = toml::parse("name = \"key\"\ncontrol = true\n");
		auto modifier = IModifier::fromToml(tbl);

		Value gated {1.0f, ValueType::axis_1d};
		EvalContext no_mods;
		modifier->apply(gated, no_mods);
		assert(approx(gated.as<float>(), 0.0f));

		Value passed {1.0f, ValueType::axis_1d};
		EvalContext with_ctrl;
		with_ctrl.mods = ModifierKey::control;
		modifier->apply(passed, with_ctrl);
		assert(approx(passed.as<float>(), 1.0f));
	}
}
