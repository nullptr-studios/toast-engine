#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/01_unconnected_nodes", test_dependency_graph_01_unconnected_nodes) {
	toast::World world;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	addStageFunction(*a, Stage::tick);
	addStageFunction(*b, Stage::tick);

	world.computeDependencyGraph();

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("a"), item("b")})}));
	assertScheduleEquals(scheduleFor(world, Stage::early_tick), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::post_physics), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::late_tick), schedule({}));
}
