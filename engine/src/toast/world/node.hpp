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
#include "uuid.hpp"
#include "function_table.hpp"

#include <toast/events/listener.hpp>
#include <toast/export.hpp>

namespace toast {

enum class NodeState : uint8_t {
	null,
	root,
	cached,
	global,
	loading,
	destroy
};

enum class NodeType : uint8_t {
	null,
	world_root,
	root,
	child
};

class TOAST_API Node {
public:
	[[nodiscard]]
	/// @brief Returns the serialized unique identifier of this node
	auto uuid() const noexcept -> UUID;

	[[nodiscard]]
	/// @brief Returns the name of this node
	auto name() const noexcept -> std::string;

	/// @brief Setter for the node
	void name(std::string_view name) noexcept;

	[[nodiscard]]
	auto enabled() const noexcept -> bool;
	void enabled(bool value) noexcept;

	// TODO: box()

	[[nodiscard]]
	auto parent() const noexcept -> Node*;

protected:
	// listener is lazily initialized
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

private:
	struct {
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
