#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/thread_pool.hpp"

#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace toast::tests::scripting_tests;

namespace {

std::atomic<size_t> g_on_caller {0};
std::atomic<size_t> g_on_workers {0};

/// The C++ tick of the nodes below, it runs in the same job as the Lua tick of its node
void countThread(void* /*node*/) {
	(toast::ThreadPool::onWorkerThread() ? g_on_workers : g_on_caller).fetch_add(1, std::memory_order_relaxed);
}

/// Every worker of the pool is waiting for work, which is how a frame normally finds it
void waitForIdlePool() {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (toast::ThreadPool::idleWorkers() < toast::ThreadPool::workerCount() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}
}

constexpr const char* k_counting_tick = R"lua(
local M = {}
M.ticks = 0
function M:tick() M.ticks = M.ticks + 1 end
return M
)lua";

}

// The thread that runs a frame hands the waves to the pool and waits for them, so scripts tick on the workers, like the
// C++ ticks always did. It only steps in when the pool cannot take the work, see 14g
TOAST_TEST_NAMED("Scripting", "scripting/18_ticks_run_on_pool_workers", test_scripting_18_scripted_ticks_run_on_pool_workers) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int scripted_count = 40;
	constexpr int plain_count = 10;
	constexpr int frame_count = 50;

	std::vector<toast::Box<toast::Node>> scripted;
	uint64_t group = 0;
	for (int i = 0; i < scripted_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "s" + std::to_string(i));
		toast::_detail::WorldTestAccess::setTickCallback(*node, &countThread);
		if (i % 5 == 0) {
			group = ::scripting::LuaState::newGroup();
		}
		toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(k_counting_tick, 4000 + i), group);
		scripted.push_back(node);
	}
	for (int i = 0; i < plain_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "p" + std::to_string(i));
		toast::_detail::WorldTestAccess::setTickCallback(*node, &countThread);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	waitForIdlePool();
	g_on_caller = 0;
	g_on_workers = 0;
	for (int frame = 0; frame < frame_count; ++frame) {
		waitForIdlePool();
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}

	assert(g_on_caller == 0);
	assert(g_on_workers == static_cast<size_t>((scripted_count + plain_count) * frame_count));
	for (auto& node : scripted) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("ticks")) == frame_count);
	}
}
