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
#include "box.hpp"
#include "control_box.hpp"

#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace toast {
namespace _detail {
struct NodeCluster;
}

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
	friend class World;
	friend struct _detail::ControlBox;
	friend struct _detail::NodeCluster;
	friend struct toast::_detail::WorldTestAccess;

	Node() = default;
	~Node() = default;

public:
	[[nodiscard]]
	/// @brief Returns the serialized unique identifier of this node
	auto uid() const noexcept -> const UID&;

	[[nodiscard]]
	/// @brief Returns the name of this node
	auto name() const noexcept -> std::string_view;

	/// @brief Setter for the node
	void name(std::string_view name) noexcept;

	[[nodiscard]]
	auto enabled() const noexcept -> bool;
	void enabled(bool value) noexcept;

	[[nodiscard]]
	auto box() const noexcept -> Box<Node>;

	[[nodiscard]]
	auto parent() noexcept -> Box<Node>;

	auto addChild() -> Box<Node>;

protected:
	// listener is lazily initialized
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

private:
	struct {
		UID uid;    // serialized unique id
		std::string name;
		NodeState state = NodeState::null;
		NodeType type = NodeType::null;
		bool local_enabled = false;        // is this object enabled?
		bool inherited_enabled = false;    // is any parent of this object enabled?
		std::array<uint8_t, 4> wave = {255, 255, 255, 255};
		Box<Node> box;
		Box<Node> parent;
		std::vector<Box<Node>> children;
		std::unique_ptr<event::Listener> listener = nullptr;
	} m;

	NodeFunctionTable* table = nullptr;

	void inheritedEnabled(bool value) noexcept;
};

}
