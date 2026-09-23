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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <toast/export.hpp>
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

}
