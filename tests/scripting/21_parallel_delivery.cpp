#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/thread_pool.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <mutex>
#include <vector>

using namespace toast::tests::scripting_tests;
using Clock = std::chrono::steady_clock;

namespace {

struct Delivery {
	size_t interpreter = 0;
	int sequence = 0;
	Clock::time_point start;
	Clock::time_point end;
	bool on_worker = false;
};

void spinFor(std::chrono::microseconds time) {
	const auto deadline = Clock::now() + time;
	while (Clock::now() < deadline) { }
}

}

// The calls that had to wait for a busy interpreter are delivered one job per interpreter on the pool. The calls into one
// interpreter keep the order they were made in, the ones into different interpreters run at the same time, and the thread
// that delivers does not run them itself
TOAST_TEST_NAMED("Scripting", "scripting/21_queued_calls_are_delivered_in_parallel", test_scripting_21_queued_calls_are_delivered_in_parallel) {
	luaState();
	toast::_detail::WorldTestAccess::initThreadPool();
	::scripting::ScriptDispatch::clear();

	const size_t interpreter_count = std::min<size_t>(6, ::scripting::LuaState::get().poolSize());
	constexpr int calls_per_interpreter = 4;
	std::mutex mutex;
	std::vector<Delivery> deliveries;

	// Queued the way a busy interpreter queues them: round by round over the interpreters
	for (int sequence = 0; sequence < calls_per_interpreter; ++sequence) {
		for (size_t interpreter = 0; interpreter < interpreter_count; ++interpreter) {
			::scripting::ScriptDispatch::enqueue(interpreter, [&, interpreter, sequence] {
				Delivery delivery {.interpreter = interpreter, .sequence = sequence, .start = Clock::now()};
				delivery.on_worker = toast::ThreadPool::onWorkerThread();
				spinFor(std::chrono::microseconds(2000));
				delivery.end = Clock::now();
				std::scoped_lock lock(mutex);
				deliveries.push_back(delivery);
			});
		}
	}

	const size_t delivered = ::scripting::ScriptDispatch::deliver();
	assert(delivered == interpreter_count * calls_per_interpreter);
	assert(::scripting::ScriptDispatch::pending() == 0);
	assert(deliveries.size() == interpreter_count * calls_per_interpreter);

	// Per interpreter, in the order they were queued
	for (size_t interpreter = 0; interpreter < interpreter_count; ++interpreter) {
		int expected = 0;
		for (const Delivery& delivery : deliveries) {
			if (delivery.interpreter == interpreter) {
				assert(delivery.sequence == expected);
				++expected;
			}
		}
		assert(expected == calls_per_interpreter);
	}

	// Different interpreters at the same time
	bool overlapped = false;
	for (const Delivery& first : deliveries) {
		for (const Delivery& second : deliveries) {
			if (first.interpreter != second.interpreter && first.start < second.end && second.start < first.end) {
				overlapped = true;
			}
		}
	}
	assert(overlapped || interpreter_count < 2 || toast::ThreadPool::workerCount() < 2);

	// On the pool, not on the thread that asked
	size_t on_workers = 0;
	for (const Delivery& delivery : deliveries) {
		on_workers += delivery.on_worker ? 1 : 0;
	}
	assert(on_workers == deliveries.size());
}
