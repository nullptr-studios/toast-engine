/**
 * @file node.hpp
 * @author Dante Harper & Xein
 * @date 29 Apr 2026
 *
 * TODO: handle the on enable and on disable and begin and end with event::listener
 * nodes in cache should be disabled always
 *
 */

#pragma once
#include "function_table.hpp"
#include "uuid.hpp"

#include <toast/events/listener.hpp>
#include <toast/export.hpp>

namespace toast {

enum class NodeState : uint8_t {
	null,
	root,       ///< The node is currently in use within world::root_node
	cached,     ///< The node is loaded in memory but not in use
	global,     ///< The node is currently in use within world::global[]
	loading,    ///< The node is in the load queue
	destroy,    ///< The node is in the destroy queue
};

enum class NodeType : uint8_t {
	null,
	child,         ///< This node is a regular node
	root,          ///< This node is a root node
	world_root,    ///< This node is the root that resides in the world
};

class TOAST_API Node {
public:
	[[nodiscard]]
	/// @brief Returns the serialized unique identifier of this node
	auto uuid() const noexcept -> const UUID&;

	[[nodiscard]]
	/// @brief Returns the name of this node
	auto name() const noexcept -> std::string_view;

	/// @brief Setter for the node
	void name(std::string_view name) noexcept;

	[[nodiscard]]
	auto enabled() const noexcept -> bool;
	void enabled(bool value) noexcept;

	// TODO: box()

	[[nodiscard]]
	auto parent() const noexcept -> Node*;

	UUID public_uuid;    // serialized unique id
	std::string public_name;

protected:
	// listener is lazily initialized
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

private:
	struct M {
		UUID uuid;    // serialized unique id
		std::string name;
		NodeState state = NodeState::null;
		NodeType type = NodeType::null;
		bool local_enabled;        // is this object enabled?
		bool inherited_enabled;    // is any parent of this object enabled?
		// TODO: Box<Node> box;
		Node* parent;
		std::vector<Node> children;    // TODO: std::vector<Box<Node>> children;
		std::unique_ptr<event::Listener> listener;
	} m;

	NodeFunctionTable* table;

	void inheritedEnabled(bool value) noexcept;
};

}
