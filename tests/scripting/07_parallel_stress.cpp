#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <any>
#include <cassert>
#include <vector>

using namespace toast::tests::scripting_tests;

// Hundreds of scripted nodes tick in parallel through the real scheduler while calling each other across interpreters.
// Every call has to arrive exactly once, whether it ran right away or waited in the dispatch queue, and nothing may
// touch an interpreter its thread does not own (the debug ownership assertions abort the process if one does)
TOAST_TEST_NAMED("Scripting", "scripting/07_parallel_stress", test_scripting_07_parallel_stress) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int node_count = 120;
	constexpr int frame_count = 60;

	std::vector<toast::Box<toast::Node>> nodes;
	for (int i = 0; i < node_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "n" + std::to_string(i));
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
)lua", 10 + i));
		nodes.push_back(node);
	}
	for (int i = 0; i < node_count; ++i) {
		nodes[i]->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(nodes[(i + 1) % node_count])});
	}

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);
	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}
	while (::scripting::ScriptDispatch::pending() != 0) {
		::scripting::ScriptDispatch::deliver();
	}

	for (int i = 0; i < node_count; ++i) {
		assert(std::any_cast<int>(nodes[i]->scriptRuntime()->getVar("ticks")) == frame_count);
		// every node is the target of exactly one other node
		assert(std::any_cast<int>(nodes[i]->scriptRuntime()->getVar("bumps")) == frame_count);
	}
}
