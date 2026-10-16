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

class TOAST_API AABBTree {
public:
	explicit AABBTree(size_t expected_shapes = 0);

	[[nodiscard]]
	auto insert(ShapeID shape, const AABB& bounds) -> TreeNodeID;
	void remove(TreeNodeID leaf_id);
	[[nodiscard]]
	auto updateLeaf(TreeNodeID leaf_id, const AABB& tight_bounds) -> bool;
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
	void detachLeaf(TreeNodeID leaf_id);
	void refitAncestors(TreeNodeID node_id);
	auto balance(TreeNodeID node_id) -> TreeNodeID;
	void recalculate(TreeNodeID node_id);

	std::vector<TreeNode> m_nodes;
	TreeNodeID m_root = null_node;
	TreeNodeID m_free_list = null_node;
	size_t m_active_node_count = 0;
};

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
