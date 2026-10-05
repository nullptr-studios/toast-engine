/**
 * @file script_dispatch.hpp
 * @author Xein
 * @date 5 Oct 2026
 *
 * @brief Queue of Lua work that could not run right away because its interpreter was busy
 *
 * A thread that owns one interpreter never waits for a second one, see LuaState. When a script calls into a node
 * whose interpreter is busy on another thread the call is queued here and delivered later by a thread that
 * owns nothing: between tick waves, after the event queue was polled and at the end of the frame
 *
 * Delivery order is the order of enqueueing, so the calls of one caller keep their order
 */

#pragma once

#include <cstddef>
#include <functional>
#include <toast/export.hpp>

namespace scripting {

class TOAST_API ScriptDispatch {
public:
	/// Queues `job`, which runs while the interpreter `vm_index` is owned. The job has to validate whatever it captured
	static void enqueue(size_t vm_index, std::move_only_function<void()> job);

	/// Number of jobs waiting for delivery
	[[nodiscard]]
	static auto pending() noexcept -> size_t;

	/**
	 * @brief Runs the queued work on the calling thread
	 * @param max_rounds work queued by delivered work is delivered in further rounds, up to this many
	 * @return how many jobs ran
	 *
	 * Does nothing when the calling thread owns an interpreter, because it could not wait for the others
	 */
	static auto deliver(int max_rounds = 4) -> size_t;

	/// Drops everything that is still queued without running it; used when the world is torn down
	static void clear() noexcept;
};

}
