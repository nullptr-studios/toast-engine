#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/scripting/script_runtime.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace toast::tests::scripting_tests;

namespace {

/// Makes `count` scripted nodes in one script group (0 for none) and counts how many runtimes each interpreter got
auto place(toast::World& world, std::vector<toast::Box<toast::Node>>& keep, int count, uint64_t group) -> std::map<size_t, int> {
	static uint64_t next_uid = 4000;
	std::map<size_t, int> per_interpreter;
	for (int i = 0; i < count; ++i) {
		auto node = toast::_detail::WorldTestAccess::createNode(world, "placed" + std::to_string(next_uid));
		const auto script = makeScript("local M = {} function M:tick() end return M", next_uid++);
		if (group == 0) {
			toast::_detail::WorldTestAccess::attachScript(*node, script);
		} else {
			toast::_detail::WorldTestAccess::attachScriptInGroup(*node, script, group);
		}
		++per_interpreter[node->scriptRuntime()->stateIndex()];
		keep.push_back(node);
	}
	return per_interpreter;
}

}

// The scripts of one prefab instance call each other all the time, so they share an interpreter: those calls are then
// plain calls that nothing else can be running at the same time
TOAST_TEST_NAMED("Scripting", "scripting/13_script_placement", test_scripting_13_a_small_group_shares_an_interpreter) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	std::vector<toast::Box<toast::Node>> keep;

	const auto per_interpreter = place(*world_owner, keep, 5, ::scripting::LuaState::newGroup());
	assert(per_interpreter.size() == 1);
}

// A group with hundreds of scripts (the loose nodes of a level) on one interpreter could only ever tick one script after
// the other, while the rest of the pool sits idle. Past a handful per interpreter a group is spread over several
TOAST_TEST_NAMED("Scripting", "scripting/13b_a_big_group_is_spread", test_scripting_13b_a_big_group_is_spread) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	std::vector<toast::Box<toast::Node>> keep;

	const auto per_interpreter = place(*world_owner, keep, 20, ::scripting::LuaState::newGroup());
	assert(per_interpreter.size() >= 3);
	for (const auto& [interpreter, count] : per_interpreter) {
		assert(count <= 8);
	}
}

// Scripts that belong to no group go to the interpreter that hosts the fewest, which keeps the pool balanced whatever it
// hosts already
TOAST_TEST_NAMED("Scripting", "scripting/13c_ungrouped_scripts_go_to_the_least_loaded_interpreter", test_scripting_13c_ungrouped_scripts_go_to_the_least_loaded_interpreter) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	std::vector<toast::Box<toast::Node>> keep;

	auto& pool = ::scripting::LuaState::get();
	for (size_t i = 0; i < pool.poolSize() * 3; ++i) {
		const auto before = pool.loads();
		const auto placed = place(*world_owner, keep, 1, 0);
		assert(placed.size() == 1);
		assert(before[placed.begin()->first] == *std::ranges::min_element(before));
	}
}
