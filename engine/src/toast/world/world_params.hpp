/**
 * @file world_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How the world ticks its nodes across threads
 */

#pragma once
#include <cstdint>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace toast {

struct WorldParams : params::Group<"world"> {
	template<typename T>
	using Param = params::Param<T>;
	using Kind = params::Kind;

	Param<int32_t> queued_helpers_per_worker {
	  "scheduler.queued_helpers",
	  4,
	  {.name = "Helpers per worker",
		  .min = 1,
		  .max = 64,
		  .kind = Kind::advanced,
		  .description = "Jobs a wave queues for each worker thread"}
	};
	Param<int32_t> grace_idle {
	  "scheduler.grace_idle",
	  2000,
	  {.name = "Grace with idle workers",
		  .min = 0,
		  .max = 20000,
		  .unit = "µs",
		  .kind = Kind::advanced,
		  .description = "How long a wave waits for a helper while some are free"}
	};
	Param<int32_t> grace_busy {
	  "scheduler.grace_busy",
	  150,
	  {.name = "Grace with busy workers",
		  .min = 0,
		  .max = 20000,
		  .unit = "µs",
		  .kind = Kind::advanced,
		  .description = "How long a wave waits for a helper while all are busy"}
	};
};

TOAST_API auto worldParams() -> WorldParams&;

}
