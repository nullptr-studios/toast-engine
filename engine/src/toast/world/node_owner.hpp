/**
 * @file NodeOwner.hpp
 * @author Xein
 * @date 11 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "box.hpp"

#include <mutex>
#include <toast/assets/prefab.hpp>
#include <toast/export.hpp>
#include <unordered_set>

namespace toast {
class TOAST_API NodeOwner {
public:
	NodeOwner() = default;
	~NodeOwner() = default;

	virtual void registerDependency(Node& from, Node& to) = 0;
	virtual auto requestRuntimeCreation(Node& parent) -> Box<Node> = 0;

protected:
	/// Creates a node and stores it in memory
	auto nodeAllocation(std::optional<assets::Prefab::BasicNode> node_data = std::nullopt) noexcept -> Box<Node>;

	auto buildTree(std::vector<Box<Node>>&& nodes, const assets::AssetHandle<assets::Prefab>& file) -> Box<Node>;

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
