#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/06_pruning", test_dependency_graph_06_pruning) {
	toast::World world;

	auto source = toast::_detail::WorldTestAccess::createNode(world, "source");
	auto pruned = toast::_detail::WorldTestAccess::createNode(world, "pruned");
	auto sink = toast::_detail::WorldTestAccess::createNode(world, "sink");
	auto cached = toast::_detail::WorldTestAccess::createNode(world, "cached", toast::NodeState::cached);
	addStageFunction(*source, Stage::tick);
	addStageFunction(*sink, Stage::tick);
	addStageFunction(*cached, Stage::tick);

	toast::World::registerDependency(*source, *pruned);
	toast::World::registerDependency(*pruned, *sink);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("source"), item("sink")})}));
	assertScheduleEquals(scheduleFor(world, Stage::early_tick), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::post_physics), schedule({}));
	assertScheduleEquals(scheduleFor(world, Stage::late_tick), schedule({}));
}
