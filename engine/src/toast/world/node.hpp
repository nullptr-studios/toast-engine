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
#include "box.hpp"
#include "control_box.hpp"
#include "reflect.hpp"

#include <memory>
#include <toast/assets/prefab.hpp>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>

namespace toast {
class NodeOwner;

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
	friend class NodeOwner;
	friend class World;
	friend class Workspace;
	friend class Node3D;
	friend class assets::Prefab;
	friend struct _detail::ControlBox;
	friend struct _detail::NodeCluster;
	friend struct toast::_detail::WorldTestAccess;

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

	[[nodiscard]]
	auto info() const -> const NodeInfo*;

	[[nodiscard]]
	auto sourcePrefab() const noexcept -> const assets::AssetHandle<assets::Prefab>&;

	[[nodiscard]]
	auto isInstanceRoot() const noexcept -> bool;

	[[nodiscard]]
	auto root() const noexcept -> Box<Node>;

	[[nodiscard]]
	auto find(std::string_view query) -> Box<Node>;

	[[nodiscard]]
	auto search(std::string_view query) -> std::vector<Box<Node>>;

	void spawn(UID prefab);

	Node() = default;
	virtual ~Node() = default;

	[[nodiscard]]
	auto listener() noexcept -> event::Listener&;

protected:
	NodeOwner* m_owner = nullptr;

private:
	[[Reflect, Name("UID")]]
	UID m_uid;    // serialized unique id
	[[Reflect, Name("Name")]]
	std::string m_name;
	NodeState m_state = NodeState::null;
	NodeType m_type = NodeType::null;
	[[Reflect, Name("Enabled")]]
	bool m_local_enabled = false;        // is this object enabled?
	bool m_inherited_enabled = false;    // is any parent of this object enabled?
	std::array<uint8_t, 4> m_wave = {255, 255, 255, 255};
	Box<Node> m_box;
	[[Reflect, Name("Parent")]]
	Box<Node> m_parent;
	[[Reflect, Name("Prefab")]]
	assets::AssetHandle<assets::Prefab> m_source_prefab;
	std::vector<Box<Node>> m_children;
	std::unique_ptr<event::Listener> m_listener = nullptr;
	const NodeInfo* m_info = nullptr;
	bool m_prefab_interior = false;
	std::shared_ptr<const assets::Prefab::BasicNode> m_unresolved_chunk;

	void init() { }

	void tick() { }

	[[nodiscard]]
	auto parentInternal() const noexcept -> Box<Node> {
		return m_parent;
	}

	void inheritedEnabled(bool value) noexcept;
	void changeNodeState(NodeState state) noexcept;

	// call a tick function from NodeInfo
	void callTick(const NodeInfo* info, TickFunctionList func_type) noexcept;

	// recursively call on children after this node
	void propagateCallTick(const NodeInfo* info, TickFunctionList func_type) noexcept;
};

template<typename T>
auto reflect_cast(Node* n) -> T* {
	if (n && n->info() && n->info()->isA(&Reflect<T>::type_info)) {
		return static_cast<T*>(n);
	}
	return nullptr;
}

}
