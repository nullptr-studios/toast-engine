/**
 * @file node.hpp
 * @author Dante Harper & Xein
 * @date 29 Apr 2026
 *
 * @brief Base object type for the scene graph
 *
 * All lifecycle dispatch goes through NodeInfo so subclasses avoid vtable overhead
 *
 * TODO: handle on_enable / on_disable / begin / end with event::Listener
 * nodes in cache should be disabled always
 */

#pragma once
#include "box.hpp"
#include "control_box.hpp"
#include "reflect.hpp"
#include "toast/log.hpp"
#include "toast/world/node_owner.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <toast/assets/prefab.hpp>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace toast {
class INodeOwner;

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

class [[ToastNode, Icon("Circle")]] TOAST_API Node {
	friend class INodeOwner;
	friend class World;
	friend class Workspace;
	friend class Node3D;
	friend class assets::Prefab;
	friend struct _detail::ControlBox;
	friend struct _detail::NodeCluster;
	friend struct toast::_detail::WorldTestAccess;

public:
	Node() = default;
	virtual ~Node() = default;

	/**
	 * @brief Stable unique identifier for this node
	 * @return The UID assigned at construction or deserialization; never changes
	 */
	[[nodiscard]]
	auto uid() const noexcept -> const UID&;

	/**
	 * @brief Display name of this node
	 * @note Siblings may share names; names are not unique identifiers
	 * @return The current display name
	 */
	[[nodiscard]]
	auto name() const noexcept -> std::string_view;

	/**
	 * @brief Sets the display name
	 * @param name New name; copied internally
	 */
	void name(std::string_view name) noexcept;

	/**
	 * @brief Whether this node is currently active
	 * @return false if either the local enabled flag or any ancestor's flag is false;
	 *         nodes in the cached state are always disabled
	 */
	[[nodiscard]]
	auto enabled() const noexcept -> bool;

	/**
	 * @brief Sets the local enabled flag and propagates the change to all children
	 * @param value Passing false disables the entire subtree without unregistering event callbacks
	 */
	void enabled(bool value) noexcept;

	/**
	 * @brief Returns an owning handle to this node
	 * @return A Box<Node> that shares ownership; always valid while the node exists; safe to store
	 */
	[[nodiscard]]
	auto box() const noexcept -> Box<Node>;

	/**
	 * @brief Reflection metadata registered by the code generator
	 * @return Pointer to the static NodeInfo for this type, or nullptr for a bare Node instance
	 *         that was never registered with NodeRegistry
	 * @see NodeRegistry::reflect()
	 */
	[[nodiscard]]
	auto info() const -> const NodeInfo*;

	/**
	 * @brief The prefab this node was instantiated from
	 * @return An empty AssetHandle for nodes created at runtime
	 */
	[[nodiscard]]
	auto sourcePrefab() const noexcept -> const assets::AssetHandle<assets::Prefab>&;

	/**
	 * @brief Whether this node is the root of a prefab instance
	 * @note find() stops traversal here so the outer tree cannot reach into a prefab's interior
	 * @see find()
	 */
	[[nodiscard]]
	auto isInstanceRoot() const noexcept -> bool;

	/**
	 * @brief The topmost ancestor that is not the world root
	 * @return An empty box if this node itself is a root node
	 */
	[[nodiscard]]
	auto root() const noexcept -> Box<Node>;

	/// @return The current NodeState (null, loading, cached, root, global, or destroy)
	[[nodiscard]]
	auto state() const noexcept -> NodeState {
		return m_state;
	}

	/// @return The current NodeType (null, child, root, or world_root)
	[[nodiscard]]
	auto type() const noexcept -> NodeType {
		return m_type;
	}

	/**
	 * @brief The event Listener owned by this node
	 * @return Reference to the Listener; lazy-allocated on the first call; not freed until the node is destroyed
	 */
	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

	/**
	 * @brief The immediate parent of this node
	 * @return An empty box if this node is a root or world root
	 */
	[[nodiscard]]
	auto parent() noexcept -> Box<Node>;

	/**
	 * @brief The ordered list of direct children
	 * @return Const reference to the internal children vector
	 * @warning Do not store raw pointers or references into this vector; it may reallocate
	 *          when children are added or removed
	 */
	[[nodiscard]]
	auto children() const noexcept -> const std::vector<Box<Node>>& {
		return m_children;
	}

	/**
	 * @brief Creates a new child node of the given type and attaches it to this node
	 * @param type Fully-qualified C++ class name, e.g. "toast::Node3D"; defaults to "toast::Node"
	 * @return The new child node, or an empty box if the type is not registered
	 * @note Only valid on nodes in the root or global state; calls the child's pre_init and init lifecycle
	 */
	auto create(std::string_view type = "toast::Node") noexcept -> Box<Node> {
		if (m_state == NodeState::root or m_state == NodeState::global) {
			return m_owner->requestRuntimeCreate(*this, type);
		}

		// TODO: implement
		TOAST_NOT_IMPLEMENTED;
		return {};
	}

	/**
	 * @brief Asynchronously instantiates a prefab as a child of this node
	 * @param uid UID of the prefab asset to load
	 * @return An empty box immediately; the child appears at the start of the next frame
	 * @note Only valid on root or global nodes; each instance receives a fresh UID
	 */
	auto spawn(UID uid) noexcept -> Box<Node> {
		if (m_state == NodeState::root or m_state == NodeState::global) {
			return m_owner->requestRuntimeSpawn(*this, uid);
		}

		// TODO: implement
		TOAST_NOT_IMPLEMENTED;
		return {};
	}

	/**
	 * @brief Asynchronously instantiates a prefab as a child of this node
	 * @param uri Virtual URI of the prefab asset, e.g. "assets://characters/knight.node"
	 * @return An empty box immediately; the child appears at the start of the next frame
	 * @note Only valid on root or global nodes; each instance receives a fresh UID
	 */
	auto spawn(std::string_view uri) noexcept -> Box<Node> {
		if (m_state == NodeState::root or m_state == NodeState::global) {
			return m_owner->requestRuntimeSpawn(*this, uri);
		}

		// TODO: implement
		TOAST_NOT_IMPLEMENTED;
		return {};
	}

	/**
	 * @brief Depth-first search for a single descendant
	 * @param query A bare name, a slash-separated path, or a node:// URI with namespace
	 *              keywords: root, world_root, global
	 * @return The first match, or an empty box if nothing was found
	 * @note Traversal stops at prefab-instance boundaries; interior nodes are opaque to find()
	 * @see search()
	 */
	[[nodiscard]]
	auto find(std::string_view query) -> Box<Node>;

	/**
	 * @brief Depth-first search for all matching descendants
	 * @param query Same syntax as find()
	 * @return All matches in depth-first order; may be empty
	 * @note Unlike find(), search() crosses prefab-instance boundaries
	 * @see find()
	 */
	[[nodiscard]]
	auto search(std::string_view query) -> std::vector<Box<Node>>;

	void addDependsOn(Node& other);
	void removeDependsOn(Node& other);

protected:
	INodeOwner* m_owner = nullptr;

private:
	[[Reflect, Hidden]]
	UID m_uid;    // serialized unique id
	std::string m_name;

	[[Reflect, Hidden]]
	bool m_local_enabled = false;        // is this object enabled?
	bool m_inherited_enabled = false;    // is any parent of this object enabled?

	[[Reflect, Name("Parent"), InspectorNoModify]]
	Box<Node> m_parent;

	[[Reflect, Name("Prefab"), InspectorNoModify]]
	assets::AssetHandle<assets::Prefab> m_source_prefab;

	NodeState m_state = NodeState::null;
	NodeType m_type = NodeType::null;
	std::array<uint8_t, 4> m_wave = {
	  255, 255, 255, 255
	};    ///< one wave index per tick phase (early/tick/post-physics/late); 255 = unscheduled

	Box<Node> m_box;
	const NodeInfo* m_info = nullptr;
	bool m_prefab_interior =
	    false;                 ///< true for nodes inside a prefab instance that are not the root; find() stops traversal here
	std::vector<Box<Node>> m_children;
	std::shared_ptr<const assets::Prefab::BasicNode>
	    m_unresolved_chunk;    ///< keeps raw prefab data alive until all asset-handle fields are resolved

	std::unique_ptr<event::Listener> m_listener = nullptr;

	[[nodiscard]]
	auto parentInternal() const noexcept -> Box<Node> {
		return m_parent;
	}

	void inheritedEnabled(bool value) noexcept;
	void changeNodeState(NodeState state) noexcept;

	/// dispatches one lifecycle phase by walking the NodeInfo chain and calling the matching invoker
	void callTick(const NodeInfo* info, TickFunctionList func_type) noexcept;

	/// calls callTick on this node then recurses into children
	void propagateCallTick(const NodeInfo* info, TickFunctionList func_type) noexcept;
};

}

/// null-safe RTTI replacement; walks the NodeInfo chain instead of dynamic_cast
template<typename T>
auto reflect_cast(toast::Node* n) -> T* {    // NOLINT
	if (n && n->info() && n->info()->isA(&toast::Reflect<T>::type_info)) {
		return static_cast<T*>(n);
	}
	return nullptr;
}
