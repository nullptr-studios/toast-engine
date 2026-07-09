#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/input/assets/haptic.hpp>
#include <toml++/toml.hpp>

namespace {
auto approx(float a, float b, float eps = 0.05f) -> bool {
	return std::abs(a - b) <= eps;
}
}

TOAST_TEST_NAMED("input", "input/07-haptics", test_input_haptics) {
	// standard mode: constant left/right intensities for a fixed duration
	{
		toml::table tbl = toml::parse(
		    "mode = \"standard\"\n"
		    "priority = 2\n"
		    "duration_ms = 200\n"
		    "left = 0.8\n"
		    "right = 0.3\n"
		);
		assets::Haptic haptic(std::move(tbl));

		assert(haptic.mode() == assets::HapticMode::standard);
		assert(haptic.priority() == 2);
		assert(haptic.durationMs() == 200);

		const glm::vec2 motors = haptic.sample(0.5f);
		assert(approx(motors.x, 0.8f));
		assert(approx(motors.y, 0.3f));
	}

	// curve mode, single channel: one embedded linear curve ramps both motors 0 -> 1
	{
		toml::table tbl = toml::parse(
		    "mode = \"curve\"\n"
		    "priority = 1\n"
		    "duration_ms = 500\n"
		    "channels = \"single\"\n"
		    "pan = 0.0\n"
		    "multiplier = 1.0\n"
		    "[curve]\n"
		    "spline_type = \"linear\"\n"
		    "dimension = \"2d\"\n"
		    "t_scale = 1.0\n"
		    "[[curve.points]]\n"
		    "x = 0.0\n"
		    "y = 0.0\n"
		    "[[curve.points]]\n"
		    "x = 1.0\n"
		    "y = 1.0\n"
		);
		assets::Haptic haptic(std::move(tbl));

		assert(haptic.mode() == assets::HapticMode::curve);
		assert(haptic.priority() == 1);

		// linear curve: intensity follows the normalized time on both motors
		assert(approx(haptic.sample(0.0f).x, 0.0f));
		assert(approx(haptic.sample(1.0f).x, 1.0f));
		assert(approx(haptic.sample(1.0f).y, 1.0f));
	}

	// curve mode, single channel with full-right pan: left motor is silenced
	{
		toml::table tbl = toml::parse(
		    "mode = \"curve\"\n"
		    "duration_ms = 100\n"
		    "channels = \"single\"\n"
		    "pan = 1.0\n"
		    "multiplier = 1.0\n"
		    "[curve]\n"
		    "spline_type = \"linear\"\n"
		    "dimension = \"2d\"\n"
		    "[[curve.points]]\n"
		    "x = 0.0\n"
		    "y = 0.0\n"
		    "[[curve.points]]\n"
		    "x = 1.0\n"
		    "y = 1.0\n"
		);
		assets::Haptic haptic(std::move(tbl));

		const glm::vec2 motors = haptic.sample(1.0f);
		assert(approx(motors.x, 0.0f));    // pan fully right mutes the left motor
		assert(approx(motors.y, 1.0f));
	}
}
