/**
 * @file Workspace.hpp
 * @author Xein
 * @date 15 Jun 2026
 *
 * @brief Reduced @c World class for the editor viewport
 *
 * Doesn't multithread anything or handle ticking
 */

#pragma once

#include "node_owner.hpp"

#include <toast/events/listener.hpp>

namespace toast {
/**
 * @brief Single-viewport node owner used by the editor
 *
 * A lightweight alternative to World: owns a node tree through the INodeOwner interface
 * but never ticks nodes, never builds a dependency graph, and never runs lifecycle functions
 * beyond those triggered by instantiation. Use it to display or edit a node tree without
 * running any game logic.
 *
 * @see World, INodeOwner
 */
class Workspace : public INodeOwner {
public:
	/**
	 * @brief Creates a new workspace rooted at a single fresh node of the given type
	 * @param type Fully-qualified C++ class name for the root node, e.g. "toast::Node3D"
	 * @param handle UID the editor assigned to this workspace; stored for round-trip serialization
	 */
	Workspace(std::string_view type, UID handle);

	/**
	 * @brief Opens an existing workspace by loading the prefab at the given UID
	 * @param uid Asset UID of a previously saved workspace prefab
	 */
	Workspace(UID uid);

	auto name() -> std::string override;

	/// No-op; Workspace has no tick scheduler and never registers dependencies
	void registerDependency(Node& from, Node& to) override;

	/// Same query grammar as World::findFrom(); searches only within m_root_node
	auto findFrom(const Node& origin, std::string_view query) -> Box<Node> override;

	/// Same query grammar as World::searchFrom(); searches only within m_root_node
	auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> override;

private:
	UID m_handle;
	Box<Node> m_root_node;
	event::Listener m_listener;

	void eventSubscriptions();

public:
	/// Intentionally empty; Workspace nodes are never ticked
	void tick() override { }
};
}
