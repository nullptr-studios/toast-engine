#pragma once

#include "world.hpp"

#include <string_view>

namespace toast::_detail {

struct WorldTestAccess {
	static auto createNode(World& world, std::string_view name, NodeState state = NodeState::root) -> Box<Node> {
		auto node = world.nodeAllocation();
		node->table = new NodeFunctionTable();
		node->m.name = name;
		node->m.state = state;
		node->m.type = NodeType::child;
		node->m.local_enabled = true;
		node->m.inherited_enabled = true;
		return node;
	}

	static auto functionTable(Node& node) noexcept -> NodeFunctionTable& { return *node.table; }

	static auto functionTable(const Node& node) noexcept -> const NodeFunctionTable& { return *node.table; }

	static auto tickSchedule(World& world) noexcept -> decltype(auto) { return (world.tick_schedule); }

	static auto dependencyGraph(World& world) noexcept -> decltype(auto) { return (world.graph); }
};

}
