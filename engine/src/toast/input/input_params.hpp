/**
 * @file input_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How the game reads the mouse
 */

#pragma once
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace input {

struct InputParams : toast::params::Group<"input"> {
	using Kind = toast::params::Kind;

	float mouse_sensitivity = param(
	    &mouse_sensitivity, "mouse.sensitivity", 1.0F,
	    {.name = "Sensitivity", .min = 0.05F, .max = 10.0F, .description = "Scales mouse movement"}
	);
	float mouse_scroll_speed = param(
	    &mouse_scroll_speed, "mouse.scroll_speed", 1.0F,
	    {.name = "Scroll speed", .min = 0.1F, .max = 10.0F, .description = "Scales the wheel"}
	);
	float gamepad_activity_threshold = param(
	    &gamepad_activity_threshold, "gamepad.activity_threshold", 0.5F,
	    {.name = "Wake threshold", .min = 0.05F, .max = 1.0F, .description = "Stick push that switches prompts to the gamepad"}
	);
	bool haptics_enabled =
	    param(&haptics_enabled, "haptics.enabled", true, {.name = "Rumble", .description = "Controller vibration"});
	float haptics_strength = param(
	    &haptics_strength, "haptics.strength", 1.0F,
	    {.name = "Rumble strength", .min = 0.0F, .max = 2.0F, .description = "Scales every rumble"}
	);
	int32_t haptics_slack = param(
	    &haptics_slack, "haptics.slack", 50,
	    {.name = "Rumble slack",
			 .min = 0,
			 .max = 500,
			 .unit = "ms",
			 .kind = Kind::advanced,
			 .description = "Extra time a rumble is kept on past each frame"}
	);
};

TOAST_API auto inputParams() -> InputParams&;

}
