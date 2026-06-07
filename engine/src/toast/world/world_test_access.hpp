#pragma once

#include "world.hpp"

#include <string_view>
#include <toast/export.hpp>

namespace toast::_detail {

struct TOAST_API WorldTestAccess {
	static auto createWorld() -> World*;

	static auto createNode(World& world, std::string_view name, NodeState state = NodeState::root) -> Box<Node>;

	static void registerDependency(Node& from, Node& to);

	// Test-only: make `node` participate in the given tick stage by attaching a fabricated
	// NodeInfo (the per-instance NodeFunctionTable no longer exists).
	static void addTickStage(Node& node, TickFunctionList stage);

	static auto tickSchedule(World& world) noexcept -> _detail::TickSchedule&;

	static auto dependencyGraph(World& world) noexcept -> World::DependencyGraph&;

	static void computeDependencyGraph(World& world);

	static auto dependencyGraphGraphviz(const World& world) -> std::string;
};

}
