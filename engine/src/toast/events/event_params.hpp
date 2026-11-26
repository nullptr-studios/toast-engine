/**
 * @file event_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief What the event system reports about itself
 */

#pragma once
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace event {

struct EventParams : toast::params::Group<"events"> {
	template<typename T>
	using Param = toast::params::Param<T>;
	using Kind = toast::params::Kind;

	Param<float> slow_warning {
	  "dispatch.slow_warning",
	  0.0F,
	  {.name = "Slow event warning",
		  .min = 0.0F,
		  .max = 1000.0F,
		  .unit = "ms",
		  .description = "Log an event whose listeners take longer than this. 0 is off"}
	};
	Param<bool> log_dispatch {
	  "dispatch.log",
	  false,
	  {.name = "Log events", .kind = Kind::debug, .description = "Write the type of every event as it is dispatched"}
	};
};

TOAST_API auto eventParams() -> EventParams&;

}
