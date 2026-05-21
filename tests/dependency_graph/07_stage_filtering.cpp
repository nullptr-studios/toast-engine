#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/07_stage_filtering", test_dependency_graph_07_stage_filtering) {
	toast::World world;

	auto early = toast::_detail::WorldTestAccess::createNode(world, "early");
	auto tick = toast::_detail::WorldTestAccess::createNode(world, "tick");
	auto post = toast::_detail::WorldTestAccess::createNode(world, "post");
	auto late = toast::_detail::WorldTestAccess::createNode(world, "late");
	addStageFunction(*early, Stage::early_tick);
	addStageFunction(*tick, Stage::tick);
	addStageFunction(*post, Stage::post_physics);
	addStageFunction(*late, Stage::late_tick);

	world.computeDependencyGraph();

	assertScheduleEquals(scheduleFor(world, Stage::early_tick), schedule({wave({item("early")})}));
	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("tick")})}));
	assertScheduleEquals(scheduleFor(world, Stage::post_physics), schedule({wave({item("post")})}));
	assertScheduleEquals(scheduleFor(world, Stage::late_tick), schedule({wave({item("late")})}));
}
