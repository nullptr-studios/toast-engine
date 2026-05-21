#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/04_multiple_subgraphs", test_dependency_graph_04_multiple_subgraphs) {
	toast::World world;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto x = toast::_detail::WorldTestAccess::createNode(world, "x");
	auto y = toast::_detail::WorldTestAccess::createNode(world, "y");
	addStageFunction(*a, Stage::tick);
	addStageFunction(*b, Stage::tick);
	addStageFunction(*x, Stage::tick);
	addStageFunction(*y, Stage::tick);

	toast::World::registerDependency(*a, *b);
	toast::World::registerDependency(*x, *y);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("a"), item("x")}), wave({item("b"), item("y")})}));
}
