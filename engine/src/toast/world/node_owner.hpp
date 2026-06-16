/**
 * @file NodeOwner.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
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

	virtual void tick() = 0;

	virtual void registerDependency(Node& from, Node& to) = 0;

	virtual auto findFrom(const Node& origin, std::string_view query) -> Box<Node> = 0;
	virtual auto searchFrom(const Node& origin, std::string_view query) -> std::vector<Box<Node>> = 0;

	auto requestRuntimeCreate(Node& parent, std::string_view type) -> Box<Node>;
	auto requestRuntimeSpawn(Node& parent, UID uid) -> Box<Node>;
	auto requestRuntimeSpawn(Node& parent, std::string_view) -> Box<Node>;

	struct InstantiateContext {
		std::vector<uint64_t> asset_chain;    // Chain of asset UIDs currently being instanciated
		std::function<assets::AssetHandle<assets::Prefab>(toast::UID)> resolver;    // Resolves a node UID to its asset
	};

protected:
	static void generateUid(Node& node);

	/// Creates a node and stores it in memory
	auto nodeAllocation(std::string_view type = "toast::Node") noexcept -> Box<Node>;
	auto nodeAllocation(const assets::Prefab::BasicNode& node_data) noexcept -> Box<Node>;

	auto buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node>;

	/// Builds a live node tree from a prefab
	auto instantiate(const assets::AssetHandle<assets::Prefab>& file, InstantiateContext& ctx) -> Box<Node>;

	/// Applies reflected values onto an existing node
	void applyFields(Node& node, const assets::Prefab::BasicNode& data);

	/// Marks a node as dead and registers it as a pending tombstone
	void releaseNode(_detail::ControlBox& control) noexcept;

	/// Sweeps fully-unreferenced tombstones out of storage
	void reapTombstones() noexcept;

	template<typename Fn>
	void forEachNode(Fn&& fn) const {
		for (const auto& cb : nodes) {
			std::forward<Fn>(fn)(cb);
		}
	}

	std::mutex nodes_mutex;
	size_t tombstones = 0;    ///< Control boxes of destroyed nodes waiting for their last Box to release them

private:
	std::unordered_set<_detail::ControlBox> nodes;
};

}
