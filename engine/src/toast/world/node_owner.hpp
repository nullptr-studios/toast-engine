/**
 * @file NodeOwner.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief Abstract owner that handles node allocation, prefab instantiation, and ControlBox lifecycle
 *
 * World and Workspace both implement this interface
 */

#pragma once
#include "box.hpp"

#include <functional>
#include <mutex>
#include <string_view>
#include <toast/assets/prefab.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <unordered_set>
#include <vector>

namespace toast {
class TOAST_API INodeOwner {
public:
	INodeOwner() = default;
	~INodeOwner() = default;
	virtual auto name() -> std::string = 0;

	virtual void tick() = 0;

	virtual void registerDependency(Node& from, Node& to) = 0;

	virtual auto findFrom(const Node& origin, std::string_view query) -> Box<Node> = 0;
	virtual auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> = 0;

	auto requestRuntimeCreate(Node& parent, std::string_view type) -> Box<Node>;
	auto requestRuntimeSpawn(Node& parent, UID uid) -> Box<Node>;
	auto requestRuntimeSpawn(Node& parent, std::string_view) -> Box<Node>;

	struct InstantiateContext {
		std::vector<uint64_t> asset_chain;    ///< UIDs of prefabs currently being instantiated; prevents infinite recursion
		std::function<assets::AssetHandle<assets::Prefab>(toast::UID)> resolver;    ///< injected loader so tests can swap in a fake
	};

protected:
	/// Assigns a fresh UID to the node; called on every spawned instance to avoid UID collisions
	static void generateUid(Node& node);

	/// Removes the "toast::" namespace prefix from a type name; used to derive default display names
	static auto stripNamespace(std::string_view type) -> std::string_view;

	/// Appends " 2", " 3", etc. to base until the name is unique among the parent's existing children
	static auto uniqueChildName(const Node& parent, std::string_view base) -> std::string;

	/**
	 * @brief Allocates a Node and its ControlBox in one contiguous block
	 * @param type Fully-qualified C++ class name looked up in NodeRegistry to find the factory;
	 *             defaults to "toast::Node"
	 * @return Owning Box<Node>; the node is in NodeState::null until explicitly placed in a tree
	 */
	auto nodeAllocation(std::string_view type = "toast::Node") noexcept -> Box<Node>;

	/**
	 * @brief Allocates a Node and initializes its name and type from a prefab data record
	 * @param node_data The BasicNode entry from the prefab file
	 * @return Owning Box<Node>; the node is in NodeState::null until explicitly placed in a tree
	 */
	auto nodeAllocation(const assets::Prefab::BasicNode& node_data) noexcept -> Box<Node>;

	/**
	 * @brief Assembles a flat list of allocated nodes into a parent/child tree
	 * @param nodes Flat list in prefab file order; index 0 becomes the tree root
	 * @param file The source prefab, kept alive until all AssetHandle fields are resolved
	 * @return The root Box<Node>
	 */
	auto buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node>;

	/**
	 * @brief Allocates and initializes a full node tree from a prefab asset
	 *
	 * Allocates every node, resolves embedded child prefabs recursively via ctx.resolver,
	 * and calls pre_init on each node before returning.
	 *
	 * @param file The prefab to instantiate
	 * @param ctx Resolver for child prefabs and the asset chain used for cycle detection;
	 *            asset_chain is mutated during the call and restored on return
	 * @return The root of the new tree, or an empty box if instantiation fails
	 */
	auto instantiate(const assets::AssetHandle<assets::Prefab>& file, InstantiateContext& ctx) -> Box<Node>;

	/// Copies reflected field values from the serialized BasicNode onto the live node; skips fields absent from NodeInfo
	void applyFields(Node& node, const assets::Prefab::BasicNode& data);

	/// Marks the ControlBox as dead and increments tombstones; does not free memory
	void releaseNode(_detail::ControlBox& control) noexcept;

	/// Sweeps the nodes set and erases any ControlBox whose ref count is zero; short-circuits if tombstones == 0
	void reapTombstones() noexcept;

	template<typename Fn>
	void forEachNode(Fn&& fn) const {
		for (const auto& cb : nodes) {
			std::forward<Fn>(fn)(cb);
		}
	}

	std::mutex nodes_mutex;
	size_t tombstones = 0;    ///< short-circuits the reap sweep when there's nothing to clean

private:
	std::unordered_set<_detail::ControlBox> nodes;
};

}
