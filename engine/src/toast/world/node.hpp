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
#include "reflect.hpp"
#include "toast/world/box.hpp"
#include "toast/world/control_box.hpp"
#include "uuid.hpp"

#include <toast/events/listener.hpp>
#include <toast/export.hpp>

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

class [[ToastNode]] TOAST_API Node {
	friend class World;
	friend struct _detail::ControlBox;
	friend struct _detail::NodeCluster;
	friend struct toast::_detail::WorldTestAccess;

	// Explicit `private:` (not the implicit class default) so the generated reflection TU,
	// which does `#define private public`, can reach the ctor/dtor for its factory.

private:
	Node() = default;
	~Node() = default;

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

	[[nodiscard]]
	auto box() const noexcept -> Box<Node>;

	[[nodiscard]]
	auto parent() noexcept -> Box<Node>;

	auto addChild() -> Box<Node>;

	[[nodiscard]]
	auto info() const -> const NodeInfo*;

protected:
	// listener is lazily initialized
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

private:
	[[Serialize, Name("UID")]]
	UUID m_uuid;    // serialized unique id
	[[Serialize, Name("Name")]]
	std::string m_name;
	NodeState m_state = NodeState::null;
	NodeType m_type = NodeType::null;
	[[Serialize, Name("Enabled")]]
	bool m_local_enabled = false;        // is this object enabled?
	bool m_inherited_enabled = false;    // is any parent of this object enabled?
	std::array<uint8_t, 4> m_wave = {255, 255, 255, 255};
	Box<Node> m_box;
	[[Serialize, Name("Parent")]]
	Box<Node> m_parent;
	std::vector<Box<Node>> m_children;
	std::unique_ptr<event::Listener> m_listener = nullptr;
	const NodeInfo* m_info = nullptr;

	void init() { }

	void tick() { }

	void inheritedEnabled(bool value) noexcept;
	void changeNodeState(NodeState state) noexcept;

	// Reflection dispatch: Call a tick/lifecycle function from NodeInfo
	void callTick(const NodeInfo* info, TickFunctionList func_type) noexcept;

	// Reflection dispatch: Recursively call on children after this node
	void propagateCallTick(const NodeInfo* info, TickFunctionList func_type) noexcept;
};

// Safe down/cross-cast using reflected RTTI; returns nullptr if `n` is not a `T`.
// The caller must have the `Reflect<T>` specialization (generated header) in scope.
template<typename T>
auto reflect_cast(Node* n) -> T* {
	if (n && n->info() && n->info()->isA(&Reflect<T>::type_info)) {
		return static_cast<T*>(n);
	}
	return nullptr;
}

}
