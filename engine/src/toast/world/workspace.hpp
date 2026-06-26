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
	Box<Node> m_focused_node;
	event::Listener m_listener;

	/// Seconds accumulated since the last InspectorContent push; throttles the editor update rate
	double m_inspector_accum = 0.0;

	void eventSubscriptions();

public:
	/**
	 * @brief Streams the focused node's reflected values to the editor at a fixed rate
	 *
	 * Workspace nodes are never ticked for game logic; this override only throttles and emits
	 * InspectorContent for the active workspace's focused node. It is a no-op when this workspace
	 * is not the active one or when no node is focused.
	 */
	void tick() override;
};
}
