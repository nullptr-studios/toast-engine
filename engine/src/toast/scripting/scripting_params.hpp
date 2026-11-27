/**
 * @file scripting_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Limits and thresholds of the Lua runtime
 */

#pragma once
#include <cstdint>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace scripting {

struct ScriptingParams : toast::params::Group<"scripting"> {
	using Kind = toast::params::Kind;

	int32_t max_call_depth = param(
	    &max_call_depth, "calls.max_depth", 48,
	    {.name = "Call depth", .min = 8, .max = 256, .description = "Nested script calls allowed before one is refused"}
	);
	int32_t parallel_batch = param(
	    &parallel_batch, "parallel.min_batch", 8,
	    {.name = "Parallel batch",
			 .min = 1,
			 .max = 1024,
			 .kind = Kind::advanced,
			 .description = "Fewer script calls than this run on the calling thread"}
	);
	int32_t parallel_lifecycle = param(
	    &parallel_lifecycle, "parallel.min_lifecycle_scripts", 16,
	    {.name = "Parallel lifecycle",
			 .min = 1,
			 .max = 4096,
			 .kind = Kind::advanced,
			 .description = "Fewer scripts than this start and stop on one thread"}
	);
};

TOAST_API auto scriptingParams() -> ScriptingParams&;

}
