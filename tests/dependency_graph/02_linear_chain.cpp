#include "dependency_graph_test_helpers.hpp"

#include "test_registry.hpp"

#include <memory>

using namespace toast::tests::dependency_graph;

TOAST_TEST_NAMED("Dependency Graph", "dependency_graph/02_linear_chain", test_dependency_graph_02_linear_chain) {
	std::unique_ptr<toast::World> world_owner(toast::_detail::WorldTestAccess::createWorld());
	toast::World& world = *world_owner;

	auto a = toast::_detail::WorldTestAccess::createNode(world, "a");
	auto b = toast::_detail::WorldTestAccess::createNode(world, "b");
	auto c = toast::_detail::WorldTestAccess::createNode(world, "c");
	addStageFunction(*a, Stage::tick);
	addStageFunction(*b, Stage::tick);
	addStageFunction(*c, Stage::tick);

	toast::_detail::WorldTestAccess::registerDependency(*a, *b);
	toast::_detail::WorldTestAccess::registerDependency(*b, *c);

	toast::_detail::WorldTestAccess::computeDependencyGraph(world);

	assertScheduleEquals(scheduleFor(world, Stage::tick), schedule({wave({item("a")}), wave({item("b")}), wave({item("c")})}));
}
