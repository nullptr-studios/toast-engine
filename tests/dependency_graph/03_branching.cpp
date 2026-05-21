#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/03_branching", test_dependency_graph_03_branching) {
	toast::World world;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto c = toast::_detail::WorldTestAccess::createNode(world, "c");
	auto d = toast::_detail::WorldTestAccess::createNode(world, "d");
	addStageFunction(*a, Stage::tick);
	addStageFunction(*b, Stage::tick);
	addStageFunction(*c, Stage::tick);
	addStageFunction(*d, Stage::tick);

	toast::World::registerDependency(*a, *b);
	toast::World::registerDependency(*a, *c);
	toast::World::registerDependency(*b, *d);
	toast::World::registerDependency(*c, *d);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(
	    scheduleFor(world, Stage::tick),
	    schedule({wave({item("a")}), wave({item("b"), item("c")}), wave({item("d")})})
	);
}
