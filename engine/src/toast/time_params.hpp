/**
 * @file time_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How fast game time runs and the longest step
 */

#pragma once
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace toast {

struct TimeParams : params::Group<"time"> {
	using Kind = params::Kind;

	float scale = param(
	    &scale, "scale", 1.0F,
	    {.name = "Time scale", .min = 0.0F, .max = 100.0F, .description = "Speed of game time. 1 is normal, 0 stops it"}
	);
	float max_delta = param(
	    &max_delta, "max_delta", 1.0F / 15.0F,
	    {.name = "Longest frame",
			 .min = 0.001F,
			 .max = 1.0F,
			 .unit = "s",
			 .description = "A slower frame is cut to this, so a hitch does not become a huge step"}
	);
};

TOAST_API auto timeParams() -> TimeParams&;

}
