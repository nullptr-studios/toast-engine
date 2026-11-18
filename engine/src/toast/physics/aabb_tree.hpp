/**
 * @file aabb_tree.hpp
 * @author Xein
 * @date 14 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "aabb.hpp"
#include "shape.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <vector>

namespace physics {

using TreeNodeID = uint32_t;
inline constexpr TreeNodeID null_node = std::numeric_limits<TreeNodeID>::max();

struct TOAST_API TreeNode {
	AABB bounds {};
	TreeNodeID parent = null_node;
	TreeNodeID left = null_node;
	TreeNodeID right = null_node;
	TreeNodeID next_free = null_node;

	ShapeID shape {};
	int height = -1;

	[[nodiscard]]
	auto isAllocated() const -> bool;

	[[nodiscard]]
	auto isLeaf() const -> bool;
};

struct TOAST_API AABBTreeDebugNode {
	AABB bounds {};
	TreeNodeID id = null_node;
	int height = -1;
	bool leaf = false;
};

struct TOAST_API AABBTreeBuildLeaf {
	ShapeID shape {};
	AABB bounds {};
};

class TOAST_API AABBTree {
public:
	explicit AABBTree(size_t expected_shapes = 0);

	[[nodiscard]]
	static auto fatten(const AABB& tight_bounds, glm::vec3 displacement = {}) -> AABB;

	[[nodiscard]]
	auto insert(ShapeID shape, const AABB& tight_bounds, glm::vec3 displacement = {}) -> TreeNodeID;
	void remove(TreeNodeID leaf_id);

	[[nodiscard]]

	auto needsUpdate(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement = {}) const -> bool;

	void reinsert(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement = {});
	[[nodiscard]]
	auto updateLeaf(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement = {}) -> bool;

	void rebuild(std::span<const AABBTreeBuildLeaf> leaves, std::span<TreeNodeID> out_leaf_nodes);

	[[nodiscard]]
	auto leafBounds(TreeNodeID leaf_id) const -> const AABB& {
		return m_nodes[leaf_id].bounds;
	}

	[[nodiscard]]
	auto leafCount() const -> size_t {
		return m_leaf_count;
	}

	[[nodiscard]]
	auto height() const -> int {
		return m_root == null_node ? 0 : m_nodes[m_root].height;
	}

	template<typename Visit>
	auto visitOverlaps(const AABB& bounds, Visit&& visit) const -> size_t;

	[[nodiscard]]
	auto query(const AABB& bounds, ShapeID ignored_shape = {}) const -> std::vector<ShapeID>;
	template<typename Visit>
	void raycast(glm::vec3 origin, glm::vec3 inv_dir, float max_distance, Visit&& visit) const;
	[[nodiscard]]
	auto debugNodes() const -> std::vector<AABBTreeDebugNode>;
	[[nodiscard]]
	auto size() const -> size_t;
	[[nodiscard]]
	auto validate() const -> bool;

private:
	[[nodiscard]]
	static auto fatMargin() -> float;

	auto allocateNode() -> TreeNodeID;
	void freeNode(TreeNodeID node_id);
	void insertLeaf(TreeNodeID leaf_id);
	[[nodiscard]]
	auto findBestSibling(const AABB& bounds) const -> TreeNodeID;
	auto buildRange(std::span<TreeNodeID> leaves, std::span<const glm::vec3> centroids, int depth) -> TreeNodeID;
	void detachLeaf(TreeNodeID leaf_id);
	void refitAncestors(TreeNodeID node_id);
	auto balance(TreeNodeID node_id) -> TreeNodeID;
	void recalculate(TreeNodeID node_id);

	std::vector<TreeNode> m_nodes;
	TreeNodeID m_root = null_node;
	TreeNodeID m_free_list = null_node;
	size_t m_active_node_count = 0;
	size_t m_leaf_count = 0;
};

template<typename Visit>
auto AABBTree::visitOverlaps(const AABB& bounds, Visit&& visit) const -> size_t {
	if (m_root == null_node) {
		return 0;
	}

	// yea do as mych as possible on a stack rather than on the heap
	std::array<TreeNodeID, 128> stack;
	std::vector<TreeNodeID> overflow;
	size_t count = 0;
	stack[count++] = m_root;
	size_t visited = 0;

	const auto push = [&](TreeNodeID id) {
		if (count < stack.size()) {
			stack[count++] = id;
		} else {
			overflow.emplace_back(id);
		}
	};

	while (count > 0 || not overflow.empty()) {
		TreeNodeID node_id = null_node;
		if (not overflow.empty()) {
			node_id = overflow.back();
			overflow.pop_back();
		} else {
			node_id = stack[--count];
		}

		++visited;
		const TreeNode& node = m_nodes[node_id];
		if (not node.bounds.overlaps(bounds)) {
			continue;
		}
		if (node.left == null_node) {
			visit(node.shape);
		} else {
			push(node.left);
			push(node.right);
		}
	}
	return visited;
}

template<typename Visit>
void AABBTree::raycast(glm::vec3 origin, glm::vec3 inv_dir, float max_distance, Visit&& visit) const {
	if (m_root == null_node) {
		return;
	}

	// Depth first with the nearer child on top
	struct Pending {
		TreeNodeID node;
		float t_enter;
	};

	std::array<Pending, 64> stack;
	size_t count = 0;

	float reach = max_distance;
	const auto enters = [&](TreeNodeID id, float& t_enter) {
		const std::optional<AABB::RayHit> hit = m_nodes[id].bounds.intersectRay(origin, inv_dir);
		if (!hit || hit->t_max < 0.0f || hit->t_min > reach) {
			return false;
		}
		t_enter = std::max(hit->t_min, 0.0f);
		return true;
	};

	float t_root = 0.0f;
	if (!enters(m_root, t_root)) {
		return;
	}
	stack[count++] = {m_root, t_root};

	while (count > 0) {
		const Pending pending = stack[--count];
		// The reach may have shrunk
		if (pending.t_enter > reach) {
			continue;
		}

		const TreeNode& node = m_nodes[pending.node];
		if (node.isLeaf()) {
			reach = std::min(reach, visit(node.shape, pending.t_enter));
			continue;
		}

		float t_left = 0.0f;
		float t_right = 0.0f;
		const bool hits_left = enters(node.left, t_left);
		const bool hits_right = enters(node.right, t_right);
		TOAST_ASSERT(count + 2 <= stack.size(), "Physics", "AABB tree ray stack is too small for the tree");
		if (count + 2 > stack.size()) {
			return;
		}

		// The farther child goes in first
		if (hits_left && hits_right) {
			if (t_left <= t_right) {
				stack[count++] = {node.right, t_right};
				stack[count++] = {node.left, t_left};
			} else {
				stack[count++] = {node.left, t_left};
				stack[count++] = {node.right, t_right};
			}
		} else if (hits_left) {
			stack[count++] = {node.left, t_left};
		} else if (hits_right) {
			stack[count++] = {node.right, t_right};
		}
	}
}

}
