#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/02_linear_chain", test_dependency_graph_02_linear_chain) {
	toast::World world;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto c = toast::_detail::WorldTestAccess::createNode(world, "c");
	addStageFunction(*a, Stage::tick);
	addStageFunction(*b, Stage::tick);
	addStageFunction(*c, Stage::tick);

	toast::World::registerDependency(*a, *b);
	toast::World::registerDependency(*b, *c);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("a")}), wave({item("b")}), wave({item("c")})}));
}
