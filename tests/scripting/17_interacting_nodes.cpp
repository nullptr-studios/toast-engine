#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/world/tick_scheduler.hpp"

#include <algorithm>
#include <any>
#include <cassert>
#include <string>
#include <vector>

using namespace toast::tests::scripting_tests;

namespace {

constexpr const char* k_empty_tick = "local M = {} function M:tick() end return M";

auto hasInterpreter(const toast::_detail::WaveJob& job, size_t interpreter) -> bool {
	return std::ranges::find(job.interpreters, interpreter) != job.interpreters.end();
}

}

// Nodes whose scripts call each other are declared with interactsWith(). A tick wave runs them in one job, so a call from
// one to the other finds the interpreter free and never has to wait for the end of the wave
TOAST_TEST_NAMED("Scripting", "scripting/17_interacting_nodes_run_in_one_job", test_scripting_17_interacting_nodes_run_in_one_job) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto c = toast::_detail::WorldTestAccess::createNode(world, "c");
	toast::_detail::WorldTestAccess::attachScript(*a, makeScript(k_empty_tick, 5000));
	toast::_detail::WorldTestAccess::attachScript(*b, makeScript(k_empty_tick, 5001));
	toast::_detail::WorldTestAccess::attachScript(*c, makeScript(k_empty_tick, 5002));
	const size_t vm_a = a->scriptRuntime()->stateIndex();
	const size_t vm_b = b->scriptRuntime()->stateIndex();
	const size_t vm_c = c->scriptRuntime()->stateIndex();
	assert(vm_a != vm_b && vm_a != vm_c && vm_b != vm_c);

	a->interactsWith(*b);
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto& waves = toast::_detail::WorldTestAccess::tickSchedule(world).tick;
	assert(waves.size() == 1);
	assert(waves[0].size() == 3);

	const auto& scheduler = toast::_detail::WorldTestAccess::scheduler(world);
	const auto jobs = toast::_detail::planWave(waves[0], toast::TickFunctionList::tick, &scheduler.interactions);
	assert(jobs.size() == 2);
	for (const auto& job : jobs) {
		if (job.items.size() == 2) {
			assert(hasInterpreter(job, vm_a) && hasInterpreter(job, vm_b) && !hasInterpreter(job, vm_c));
		} else {
			assert(job.items.size() == 1);
			assert(job.interpreters.size() == 1 && hasInterpreter(job, vm_c));
		}
	}
}

// A script declares the interaction from Lua, with the node it was handed
TOAST_TEST_NAMED("Scripting", "scripting/17b_a_script_declares_an_interaction", test_scripting_17b_a_script_declares_an_interaction) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	toast::_detail::WorldTestAccess::attachScript(*a, makeScript(R"lua(
local M = {}
function M:declare(other) self:interactsWith(other) end
function M:tick() end
return M
)lua", 5010));
	toast::_detail::WorldTestAccess::attachScript(*b, makeScript(k_empty_tick, 5011));
	const size_t vm_a = a->scriptRuntime()->stateIndex();
	const size_t vm_b = b->scriptRuntime()->stateIndex();
	assert(vm_a != vm_b);

	a->call("declare", toast::Box<toast::Node>(b));
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto& waves = toast::_detail::WorldTestAccess::tickSchedule(world).tick;
	assert(waves.size() == 1);
	const auto& scheduler = toast::_detail::WorldTestAccess::scheduler(world);
	const auto jobs = toast::_detail::planWave(waves[0], toast::TickFunctionList::tick, &scheduler.interactions);
	assert(jobs.size() == 1);
	assert(jobs[0].items.size() == 2);
	assert(hasInterpreter(jobs[0], vm_a) && hasInterpreter(jobs[0], vm_b));
}

// Every node of a ring calls the next one on every tick. Declared, they all end up in one job, so not one call finds its
// target busy: nothing is queued and nothing is skipped
TOAST_TEST_NAMED("Scripting", "scripting/17c_declared_calls_never_find_the_target_busy", test_scripting_17c_declared_calls_never_find_the_target_busy) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int node_count = 60;
	constexpr int frame_count = 40;

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
)lua", 5100 + i));
		nodes.push_back(node);
	}
	for (int i = 0; i < node_count; ++i) {
		nodes[i]->scriptRuntime()->setVar("target", std::any {toast::Box<toast::Node>(nodes[(i + 1) % node_count])});
		nodes[i]->interactsWith(*nodes[(i + 1) % node_count]);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	auto& pool = ::scripting::LuaState::get();
	const auto contention = pool.contentionCount();
	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
		assert(::scripting::ScriptDispatch::pending() == 0);
	}

	assert(pool.contentionCount() == contention);
	for (int i = 0; i < node_count; ++i) {
		assert(std::any_cast<int>(nodes[i]->scriptRuntime()->getVar("ticks")) == frame_count);
		assert(std::any_cast<int>(nodes[i]->scriptRuntime()->getVar("bumps")) == frame_count);
	}
}
