#include "dependency_graph/dependency_graph_test_helpers.hpp"
#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_dispatch.hpp"
#include "toast/scripting/script_runtime.hpp"
#include "toast/thread_pool.hpp"
#include "toast/world/tick_scheduler.hpp"

#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace toast::tests::dependency_graph;
using namespace toast::tests::scripting_tests;

namespace {

constexpr const char* k_empty_tick = "local M = {} function M:tick() end return M";

/// Owns an interpreter on another thread until released, or for at most `limit`, so a run that goes wrong cannot hang
class TimedHolder {
public:
	TimedHolder(size_t index, std::chrono::milliseconds limit) {
		m_thread = std::thread([this, index, limit] {
			auto guard = ::scripting::LuaState::get().lock(index);
			m_held = true;
			const auto deadline = std::chrono::steady_clock::now() + limit;
			while (!m_release && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
		while (!m_held) {
			std::this_thread::yield();
		}
	}

	void release() {
		m_release = true;
		if (m_thread.joinable()) {
			m_thread.join();
		}
	}

	~TimedHolder() { release(); }

	TimedHolder(const TimedHolder&) = delete;
	auto operator=(const TimedHolder&) -> TimedHolder& = delete;

private:
	std::atomic<bool> m_held {false};
	std::atomic<bool> m_release {false};
	std::thread m_thread;
};

}

// A tick wave hands every interpreter to exactly one job, so no worker ever has to wait for another one. Nodes that share
// an interpreter used to each get their own job, and the workers queued up behind each other
TOAST_TEST_NAMED("Scripting", "scripting/14_a_wave_never_waits_for_an_interpreter", test_scripting_14_a_wave_never_waits_for_an_interpreter) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int node_count = 96;
	constexpr int frame_count = 30;

	std::vector<toast::Box<toast::Node>> nodes;
	uint64_t group = 0;
	for (int i = 0; i < node_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "n" + std::to_string(i));
		if (i % 4 == 0) {
			group = ::scripting::LuaState::newGroup();
		}
		toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(R"lua(
local M = {}
M.ticks = 0
function M:tick()
    local a = 0
    for i = 1, 20000 do a = a + (i % 7) end
    M.ticks = M.ticks + 1
end
return M
)lua", 600 + i), group);
		nodes.push_back(node);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto waits = ::scripting::LuaState::get().waitCount();
	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}

	assert(::scripting::LuaState::get().waitCount() == waits);
	assert(::scripting::ScriptDispatch::pending() == 0);
	for (auto& node : nodes) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("ticks")) == frame_count);
	}
}

// Whatever mix of nodes a wave has, every one of them ticks once per frame, and no interpreter is ever wanted by two
// workers at once: a worker that found an interpreter taken would be counted in contentionCount()
TOAST_TEST_NAMED("Scripting", "scripting/14f_a_wave_keeps_every_interpreter_to_one_worker", test_scripting_14f_a_wave_keeps_every_interpreter_to_one_worker) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int scripted_count = 150;
	constexpr int plain_count = 20;
	constexpr int frame_count = 200;

	std::vector<toast::Box<toast::Node>> scripted;
	uint64_t group = 0;
	for (int i = 0; i < scripted_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "s" + std::to_string(i));
		const auto script = makeScript(R"lua(
local M = {}
M.ticks = 0
function M:tick()
    local a = 0
    for i = 1, 2000 do a = a + (i % 7) end
    M.ticks = M.ticks + 1
end
return M
)lua", 900 + i);
		if (i % 3 == 0) {
			toast::_detail::WorldTestAccess::attachScript(*node, script);    // ungrouped
		} else {
			if (i % 5 == 1) {
				group = ::scripting::LuaState::newGroup();
			}
			toast::_detail::WorldTestAccess::attachScriptInGroup(*node, script, group);
		}
		scripted.push_back(node);
	}
	for (int i = 0; i < plain_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "c" + std::to_string(i));
		addStageFunction(*node, Stage::tick);
	}

	// A chain gives the schedule several waves, and a cycle makes a cluster of two nodes on different interpreters
	for (int i = 0; i < 8; ++i) {
		toast::_detail::WorldTestAccess::registerDependency(*scripted[i], *scripted[i + 1]);
	}
	toast::_detail::WorldTestAccess::registerDependency(*scripted[20], *scripted[21]);
	toast::_detail::WorldTestAccess::registerDependency(*scripted[21], *scripted[20]);
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);
	assert(toast::_detail::WorldTestAccess::tickSchedule(world).tick.size() > 1);

	auto& pool = ::scripting::LuaState::get();
	const auto contention = pool.contentionCount();
	const auto waits = pool.waitCount();
	for (int frame = 0; frame < frame_count; ++frame) {
		toast::_detail::WorldTestAccess::runTickFrame(world);
	}

	assert(pool.contentionCount() == contention);
	assert(pool.waitCount() == waits);
	assert(::scripting::ScriptDispatch::pending() == 0);
	for (auto& node : scripted) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("ticks")) == frame_count);
	}
}

// The pool is shared with everything else the engine runs in the background, and all of its workers can be busy for a
// while. A wave is finished by the thread that runs it plus whichever workers are free, it does not wait for the ones
// that have not shown up
TOAST_TEST_NAMED("Scripting", "scripting/14g_a_wave_does_not_wait_for_busy_pool_workers", test_scripting_14g_a_wave_does_not_wait_for_busy_pool_workers) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	constexpr int node_count = 40;
	std::vector<toast::Box<toast::Node>> nodes;
	for (int i = 0; i < node_count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "n" + std::to_string(i));
		toast::_detail::WorldTestAccess::attachScript(*node, makeScript(R"lua(
local M = {}
M.ticks = 0
function M:tick() M.ticks = M.ticks + 1 end
return M
)lua", 950 + i));
		nodes.push_back(node);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	// Every worker of the pool is busy with something else
	constexpr auto busy_time = std::chrono::milliseconds(400);
	const size_t workers = toast::ThreadPool::workerCount();
	std::atomic<size_t> started {0};
	std::atomic<bool> release {false};
	std::vector<std::future<void>> busy;
	for (size_t i = 0; i < workers; ++i) {
		busy.push_back(toast::ThreadPool::push([&started, &release] {
			++started;
			const auto deadline = std::chrono::steady_clock::now() + busy_time;
			while (!release && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}));
	}
	while (started < workers) {
		std::this_thread::yield();
	}

	const auto start = std::chrono::steady_clock::now();
	toast::_detail::WorldTestAccess::runTickFrame(world);
	const auto took = std::chrono::steady_clock::now() - start;

	release = true;
	for (auto& future : busy) {
		future.get();
	}

	assert(took < busy_time / 2);
	for (auto& node : nodes) {
		assert(std::any_cast<int>(node->scriptRuntime()->getVar("ticks")) == 1);
	}
}

// The jobs of a wave: one per interpreter holding the scripted nodes that live on it, in wave order, and one per node
// that runs no Lua
TOAST_TEST_NAMED("Scripting", "scripting/14b_a_wave_plan_has_one_job_per_interpreter", test_scripting_14b_a_wave_plan_has_one_job_per_interpreter) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	// 12 scripted nodes in 3 groups of 4, and 2 nodes that only have a C++ tick
	std::vector<toast::Box<toast::Node>> nodes;
	std::map<size_t, int> per_interpreter;
	uint64_t group = 0;
	for (int i = 0; i < 12; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "s" + std::to_string(i));
		if (i % 4 == 0) {
			group = ::scripting::LuaState::newGroup();
		}
		toast::_detail::WorldTestAccess::attachScriptInGroup(*node, makeScript(k_empty_tick, 700 + i), group);
		++per_interpreter[node->scriptRuntime()->stateIndex()];
		nodes.push_back(node);
	}
	assert(per_interpreter.size() == 3);    // what the plan below is checked against: three interpreters of four nodes
	for (int i = 0; i < 2; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "c" + std::to_string(i));
		addStageFunction(*node, Stage::tick);
		nodes.push_back(node);
	}
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto& waves = toast::_detail::WorldTestAccess::tickSchedule(world).tick;
	assert(waves.size() == 1);
	assert(waves[0].size() == 14);

	const auto jobs = toast::_detail::planWave(waves[0], toast::TickFunctionList::tick);

	size_t scripted_jobs = 0;
	size_t plain_jobs = 0;
	size_t items = 0;
	for (const auto& job : jobs) {
		items += job.items.size();
		for (size_t i = 1; i < job.items.size(); ++i) {
			assert(job.items[i - 1] < job.items[i]);    // wave order
		}
		if (job.interpreters.empty()) {
			++plain_jobs;
			assert(job.items.size() == 1);
			continue;
		}
		++scripted_jobs;
		assert(job.interpreters.size() == 1);
		assert(job.items.size() == 4);
	}
	assert(scripted_jobs == 3);
	assert(plain_jobs == 2);
	assert(items == 14);
}

// A cluster ticks its nodes one after the other, so the interpreters of its nodes are tied together: whatever else
// runs on them has to be part of the same job
TOAST_TEST_NAMED("Scripting", "scripting/14c_a_cluster_keeps_its_interpreters_in_one_job", test_scripting_14c_a_cluster_keeps_its_interpreters_in_one_job) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto c = toast::_detail::WorldTestAccess::createNode(world, "c");
	toast::_detail::WorldTestAccess::attachScriptInGroup(*a, makeScript(k_empty_tick, 800), ::scripting::LuaState::newGroup());
	toast::_detail::WorldTestAccess::attachScriptInGroup(*b, makeScript(k_empty_tick, 801), ::scripting::LuaState::newGroup());
	toast::_detail::WorldTestAccess::attachScriptInGroup(*c, makeScript(k_empty_tick, 802), ::scripting::LuaState::newGroup());
	const size_t vm_a = a->scriptRuntime()->stateIndex();
	const size_t vm_b = b->scriptRuntime()->stateIndex();
	const size_t vm_c = c->scriptRuntime()->stateIndex();
	assert(vm_a != vm_b && vm_a != vm_c && vm_b != vm_c);

	toast::_detail::WorldTestAccess::registerDependency(*a, *b);
	toast::_detail::WorldTestAccess::registerDependency(*b, *a);
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	const auto& waves = toast::_detail::WorldTestAccess::tickSchedule(world).tick;
	assert(waves.size() == 1);
	assert(waves[0].size() == 2);    // the cluster of a and b, and c

	const auto jobs = toast::_detail::planWave(waves[0], toast::TickFunctionList::tick);
	assert(jobs.size() == 2);
	for (const auto& job : jobs) {
		assert(job.items.size() == 1);
		assert(job.interpreters.size() == 1 || job.interpreters.size() == 2);
		if (job.interpreters.size() == 2) {
			assert((job.interpreters[0] == vm_a && job.interpreters[1] == vm_b) || (job.interpreters[0] == vm_b && job.interpreters[1] == vm_a));
		} else {
			assert(job.interpreters[0] == vm_c);
		}
	}
}

// A state machine is a C++ node, but its callbacks are Lua functions that live on the interpreter of the script that
// added the states, so it has to run with that script's node or the two would want the same interpreter at once
TOAST_TEST_NAMED("Scripting", "scripting/14d_a_state_machine_follows_the_interpreter_of_its_script", test_scripting_14d_a_state_machine_follows_the_interpreter_of_its_script) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	auto host = toast::_detail::WorldTestAccess::createNode(world, "host");
	auto machine = toast::_detail::WorldTestAccess::createTypedNode(world, "toast::StateMachine", "machine");
	assert(machine.exists());
	toast::_detail::WorldTestAccess::attachScript(*host, makeScript(R"lua(
local M = {}
function M:setup(machine) machine:addState("idle", { tick = function() end }) end
function M:tick() end
return M
)lua"));
	const size_t host_vm = host->scriptRuntime()->stateIndex();

	assert(!machine->scriptVm(toast::TickFunctionList::tick).has_value());    // no script has given it anything yet
	host->call("setup", toast::Box<toast::Node>(machine));
	assert(machine->scriptVm(toast::TickFunctionList::tick) == std::optional<size_t> {host_vm});

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);
	const auto& waves = toast::_detail::WorldTestAccess::tickSchedule(world).tick;
	assert(waves.size() == 1);
	assert(waves[0].size() == 2);

	const auto jobs = toast::_detail::planWave(waves[0], toast::TickFunctionList::tick);
	assert(jobs.size() == 1);
	assert(jobs[0].items.size() == 2);
	assert(jobs[0].interpreters.size() == 1 && jobs[0].interpreters[0] == host_vm);
}

// A worker running a tick wave must never wait for an interpreter: if something outside the wave has it, the call is
// queued and the worker carries on
TOAST_TEST_NAMED("Scripting", "scripting/14e_a_non_blocking_thread_never_waits_for_an_interpreter", test_scripting_14e_a_non_blocking_thread_never_waits_for_an_interpreter) {
	luaState();
	const size_t vm = ::scripting::LuaState::get().poolSize() - 1;
	TimedHolder busy(vm, std::chrono::milliseconds(400));

	const auto contention = ::scripting::LuaState::get().contentionCount();
	{
		const ::scripting::LuaState::NonBlockingScope no_waiting;
		const auto start = std::chrono::steady_clock::now();
		auto guard = ::scripting::LuaState::get().lock(vm);
		const auto waited = std::chrono::steady_clock::now() - start;
		assert(!guard);
		assert(waited < std::chrono::milliseconds(100));
	}
	assert(::scripting::LuaState::get().contentionCount() == contention + 1);

	// outside the scope a thread that owns nothing still waits its turn
	busy.release();
	auto guard = ::scripting::LuaState::get().lock(vm);
	assert(guard);
}
