#include "dependency_graph/dependency_graph_test_helpers.hpp"
#include "scripting_test_helpers.hpp"
#include "test_registry.hpp"

#include <cassert>

using namespace toast::tests::dependency_graph;
using namespace toast::tests::scripting_tests;

// A node whose only tick() lives in a script must participate in the dependency graph
// exactly like a node with a C++ tick; nodes without any tick stay pruned.
TOAST_TEST_NAMED("Scripting", "scripting/02_lua_tick_scheduling", test_scripting_02_lua_tick_scheduling) {
	luaState();
	auto world_owner = toast::_detail::WorldTestAccess::createWorld();
	toast::World& world = *world_owner;

	auto scripted = toast::_detail::WorldTestAccess::createNode(world, "scripted");
	auto plain = toast::_detail::WorldTestAccess::createNode(world, "plain");

	toast::_detail::WorldTestAccess::attachScript(*scripted, makeScript(R"lua(
local M = {}
function M:tick() end
return M
)lua"));

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	// the scripted node is scheduled for tick; the plain node is pruned everywhere
	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("scripted")})}));
	assertScheduleEquals(scheduleFor(world, Stage::early_tick), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::post_physics), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::late_tick), schedule({}));

	// a lua tick orders against a C++ tick through the same dependency edges
	auto cpp_node = toast::_detail::WorldTestAccess::createNode(world, "cpp");
	addStageFunction(*cpp_node, Stage::tick);
	toast::_detail::WorldTestAccess::registerDependency(*cpp_node, *scripted);
	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("cpp")}), wave({item("scripted")})}));
}
