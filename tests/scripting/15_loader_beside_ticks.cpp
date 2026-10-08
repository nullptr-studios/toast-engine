#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <atomic>
#include <cassert>
#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace toast::tests::scripting_tests;

// Loading runs Lua on threads of its own while the frame keeps ticking. A loader here builds scripted nodes, runs their
// init (which calls into nodes that are ticking) and lets the older ones go, on the same interpreters the ticks use.
// Nothing may touch an interpreter its thread does not own (the debug ownership assertions abort the process if one
// does), nothing may hang, and every call has to arrive exactly once
TOAST_TEST_NAMED("Scripting", "scripting/15_a_loader_thread_beside_ticking_nodes", test_scripting_15_a_loader_thread_beside_ticking_nodes) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int ticking_count = 80;
	constexpr int frame_count = 120;
	constexpr size_t kept_alive = 6;

	std::vector<toast::Box<toast::Node>> ticking;
	for (int i = 0; i < ticking_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "t" + std::to_string(i));
		toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
M.target = false
M.ticks = 0
M.bumps = 0
function M:tick()
    M.ticks = M.ticks + 1
    self.target:call("bump")
end
function M:bump()
    M.bumps = M.bumps + 1
end
return M
)lua", 3000 + i));
		ticking.push_back(node);
	}
	for (int i = 0; i < ticking_count; ++i) {
		ticking[i]->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(ticking[(i + 1) % ticking_count])});
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto spawned_script = makeScript(R"lua(
local M = {}
M.target = false
M.ready = 0
function M:init()
    M.ready = 1
    self.target:call("bump")
end
return M
)lua", 3500);

	std::atomic<bool> stop {false};
	std::atomic<int> spawned {0};
	std::thread loader([&] {
		std::deque<toast::Box<toast::Node>> alive;
		int serial = 0;
		while (!stop) {
			auto node = toast::_detail::WorldTestAccess::createNode(world, "spawned" + std::to_string(serial));
			toast::_detail::WorldTestAccess::attachScript(*node, spawned_script);
			node->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(ticking[static_cast<size_t>(serial) % ticking_count])});
			toast::_detail::WorldTestAccess::callTick(*node, toast::TickFunctionList::init);
			assert(std::any_cast<int>(node->scriptRuntime()->getVar("ready")) == 1);
			++serial;
			++spawned;

			// The oldest one goes away while the others are still around
			alive.push_back(node);
			if (alive.size() > kept_alive) {
				alive.front()->setScripts({});
				alive.pop_front();
			}
		}
	});

	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}
	stop = true;
	loader.join();
	while (::scripting::ScriptDispatch::pending() != 0) {
		::scripting::ScriptDispatch::deliver();
	}

	assert(spawned > 0);
	int bumps = 0;
	for (auto& node : ticking) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("ticks")) == frame_count);
		bumps += std::any_cast<int>(node->scriptRuntime()->getVar("bumps"));
	}
	// One from every tick of every node, and one from the init of every node the loader made
	assert(bumps == ticking_count * frame_count + spawned);
}
