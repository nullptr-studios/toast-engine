#include "script_dispatch.hpp"

#include "lua_state.hpp"

#include <atomic>
#include <mutex>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace scripting {

namespace {

struct DispatchJob {
	size_t vm_index = 0;
	std::move_only_function<void()> run;
};

// Leaked on purpose: queued jobs hold node handles and Lua references that must not be destroyed during static teardown
auto& g_dispatch_mutex = *new std::mutex();                  // NOLINT
auto& g_dispatch_queue = *new std::vector<DispatchJob>();    // NOLINT
std::atomic<size_t> g_dispatch_pending {0};                  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
thread_local bool t_dispatch_delivering = false;             // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}

void ScriptDispatch::enqueue(size_t vm_index, std::move_only_function<void()> job) {
	if (!job) {
		return;
	}
	std::scoped_lock lock(g_dispatch_mutex);
	g_dispatch_queue.push_back(DispatchJob {.vm_index = vm_index, .run = std::move(job)});
	g_dispatch_pending.store(g_dispatch_queue.size(), std::memory_order_release);
}

auto ScriptDispatch::pending() noexcept -> size_t {
	return g_dispatch_pending.load(std::memory_order_acquire);
}

auto ScriptDispatch::deliver(int max_rounds) -> size_t {
	if (g_dispatch_pending.load(std::memory_order_acquire) == 0 || t_dispatch_delivering || !LuaState::exists() ||
	    LuaState::ownsAnyState()) {
		return 0;
	}

	ZoneScopedN("Lua dispatch");    // NOLINT
	t_dispatch_delivering = true;
	size_t delivered = 0;
	for (int round = 0; round < max_rounds; ++round) {
		std::vector<DispatchJob> batch;
		{
			std::scoped_lock lock(g_dispatch_mutex);
			batch.swap(g_dispatch_queue);
			g_dispatch_pending.store(0, std::memory_order_release);
		}
		if (batch.empty()) {
			break;
		}
		for (DispatchJob& job : batch) {
			// This thread owns nothing, so lock() waits for the interpreter
			LuaState::Lock guard = LuaState::get().lock(job.vm_index);
			if (!guard) {
				continue;
			}
			job.run();
			++delivered;
		}
	}
	t_dispatch_delivering = false;
	return delivered;
}

void ScriptDispatch::clear() noexcept {
	std::vector<DispatchJob> dropped;
	{
		std::scoped_lock lock(g_dispatch_mutex);
		dropped.swap(g_dispatch_queue);
		g_dispatch_pending.store(0, std::memory_order_release);
	}
}

}
