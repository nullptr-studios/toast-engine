#include "aabb_tree.hpp"

#include "physics_settings.hpp"

#include <cmath>

namespace physics {

namespace {

constexpr int k_sah_bins = 16;

constexpr int k_max_sah_depth = 16;

auto center(const AABB& bounds) -> glm::vec3 {
	return (bounds.min + bounds.max) * 0.5f;
}

}

auto AABBTree::fatMargin() -> float {
	return tunables().broadphase_fat_margin;
}

auto TreeNode::isAllocated() const -> bool {
	return height >= 0;
}

auto TreeNode::isLeaf() const -> bool {
	return isAllocated() && left == null_node && right == null_node;
}

AABBTree::AABBTree(size_t expected_shapes) {
	constexpr auto max_expected_shapes = static_cast<size_t>(null_node) / 2;
	if (expected_shapes > max_expected_shapes) {
		throw std::length_error("AABBTree capacity exceeds the TreeNodeID range");
	}

	m_nodes.reserve(expected_shapes * 2);
}

auto AABBTree::allocateNode() -> TreeNodeID {
	TreeNodeID result = null_node;
	if (m_free_list != null_node) {
		result = m_free_list;
		m_free_list = m_nodes[result].next_free;
	} else {
		if (m_nodes.size() >= null_node) {
			throw std::length_error("AABBTree has exhausted the TreeNodeID range");
		}

		result = static_cast<TreeNodeID>(m_nodes.size());
		m_nodes.emplace_back();
	}

	auto& node = m_nodes[result];
	node = TreeNode {};
	node.height = 0;
	++m_active_node_count;
	return result;
}

void AABBTree::freeNode(TreeNodeID node_id) {
	const bool is_valid = node_id != null_node && node_id < m_nodes.size();
	TOAST_ASSERT(is_valid, "Physics", "Cannot free an invalid AABB tree node");
	if (not is_valid) {
		return;
	}

	auto& node = m_nodes[node_id];
	const bool can_free = node.isAllocated() && node.parent == null_node && node.left == null_node && node.right == null_node;
	TOAST_ASSERT(can_free, "Physics", "AABB tree nodes must be allocated and disconnected before being freed");
	if (not can_free) {
		return;
	}

	node = TreeNode {};
	node.next_free = m_free_list;
	m_free_list = node_id;
	--m_active_node_count;
}

auto AABBTree::fatten(const AABB& tight_bounds, glm::vec3 displacement) -> AABB {
	AABB fat = tight_bounds.expanded(fatMargin());
	for (int axis = 0; axis < 3; ++axis) {
		const float d = displacement[axis];
		if (not std::isfinite(d)) {
			continue;
		}
		if (d < 0.0f) {
			fat.min[axis] += d;
		} else {
			fat.max[axis] += d;
		}
	}
	return fat;
}

auto AABBTree::insert(ShapeID shape, const AABB& tight_bounds, glm::vec3 displacement) -> TreeNodeID {
	const TreeNodeID leaf_id = allocateNode();
	m_nodes[leaf_id].bounds = fatten(tight_bounds, displacement);
	m_nodes[leaf_id].shape = shape;
	insertLeaf(leaf_id);
	++m_leaf_count;
#if defined(TOAST_VALIDATE_AABB_TREE)
	TOAST_ASSERT(validate(), "Physics", "AABB tree validation failed after inserting a leaf");
#endif
	return leaf_id;
}

auto AABBTree::findBestSibling(const AABB& bounds) const -> TreeNodeID {
	// Box2D v3 style single path descent

	glm::vec3 bounds_center = center(bounds);
	float bounds_area = bounds.area();

	TreeNodeID index = m_root;
	float area_base = m_nodes[index].bounds.area();
	float direct_cost = combine(m_nodes[index].bounds, bounds).area();
	float inherited_cost = 0.0f;

	TreeNodeID best_sibling = index;
	float best_cost = direct_cost;

	while (not m_nodes[index].isLeaf()) {
		const auto& node = m_nodes[index];

		// cost of a new parent for this node and the leaf
		const float cost = direct_cost + inherited_cost;
		if (cost < best_cost) {
			best_sibling = index;
			best_cost = cost;
		}

		inherited_cost += direct_cost - area_base;

		const auto& child_1 = m_nodes[node.left];
		const auto& child_2 = m_nodes[node.right];
		bool leaf_1 = child_1.isLeaf();
		bool leaf_2 = child_2.isLeaf();
		float direct_cost_1 = combine(child_1.bounds, bounds).area();
		float lower_cost_1 = std::numeric_limits<float>::max();
		float area_1 = 0.0f;
		if (leaf_1) {
			float cost_1 = direct_cost_1 + inherited_cost;
			if (cost_1 < best_cost) {
				best_sibling = node.left;
				best_cost = cost_1;
			}
		} else {
			area_1 = child_1.bounds.area();
			lower_cost_1 = inherited_cost + direct_cost_1 + std::min(bounds_area - area_1, 0.0f);
		}

		float direct_cost_2 = combine(child_2.bounds, bounds).area();
		float lower_cost_2 = std::numeric_limits<float>::max();
		float area_2 = 0.0f;
		if (leaf_2) {
			const float cost_2 = direct_cost_2 + inherited_cost;
			if (cost_2 < best_cost) {
				best_sibling = node.right;
				best_cost = cost_2;
			}
		} else {
			area_2 = child_2.bounds.area();
			lower_cost_2 = inherited_cost + direct_cost_2 + std::min(bounds_area - area_2, 0.0f);
		}

		if (leaf_1 && leaf_2) {
			break;
		}
		if (best_cost <= lower_cost_1 && best_cost <= lower_cost_2) {
			break;
		}

		// Both children already contain the leaf so area cannot tell them apart
		if (lower_cost_1 == lower_cost_2 && not leaf_1) {
			glm::vec3 offset_1 = center(child_1.bounds) - bounds_center;
			glm::vec3 offset_2 = center(child_2.bounds) - bounds_center;
			lower_cost_1 = glm::dot(offset_1, offset_1);
			lower_cost_2 = glm::dot(offset_2, offset_2);
		}

		if (lower_cost_1 < lower_cost_2 && not leaf_1) {
			index = node.left;
			area_base = area_1;
			direct_cost = direct_cost_1;
		} else {
			index = node.right;
			area_base = area_2;
			direct_cost = direct_cost_2;
		}
	}

	return best_sibling;
}

void AABBTree::insertLeaf(TreeNodeID leaf_id) {
	TOAST_ASSERT(m_nodes[leaf_id].isLeaf(), "Physics", "AABB tree insertion requires a leaf node");
	TOAST_ASSERT(m_nodes[leaf_id].parent == null_node, "Physics", "AABB tree insertion requires a detached leaf");
	if (m_root == null_node) {
		m_root = leaf_id;
		return;
	}

	const TreeNodeID sibling_id = findBestSibling(m_nodes[leaf_id].bounds);

	const TreeNodeID old_parent_id = m_nodes[sibling_id].parent;
	const TreeNodeID new_parent_id = allocateNode();
	auto& new_parent = m_nodes[new_parent_id];
	new_parent.parent = old_parent_id;
	new_parent.left = sibling_id;
	new_parent.right = leaf_id;
	new_parent.bounds = combine(m_nodes[sibling_id].bounds, m_nodes[leaf_id].bounds);
	new_parent.height = 1 + std::max(m_nodes[sibling_id].height, m_nodes[leaf_id].height);

	m_nodes[sibling_id].parent = new_parent_id;
	m_nodes[leaf_id].parent = new_parent_id;

	if (old_parent_id == null_node) {
		m_root = new_parent_id;
	} else {
		auto& old_parent = m_nodes[old_parent_id];
		TOAST_ASSERT(
		    old_parent.left == sibling_id || old_parent.right == sibling_id, "Physics", "AABB tree sibling must belong to its parent"
		);
		if (old_parent.left == sibling_id) {
			old_parent.left = new_parent_id;
		} else {
			old_parent.right = new_parent_id;
		}
	}

	refitAncestors(new_parent_id);
}

void AABBTree::remove(TreeNodeID leaf_id) {
	const bool is_valid_leaf = leaf_id != null_node && leaf_id < m_nodes.size() && m_nodes[leaf_id].isLeaf();
	TOAST_ASSERT(is_valid_leaf, "Physics", "AABBTree::remove requires an allocated leaf");
	if (not is_valid_leaf) {
		return;
	}

	detachLeaf(leaf_id);
	freeNode(leaf_id);
	--m_leaf_count;
#if defined(TOAST_VALIDATE_AABB_TREE)
	TOAST_ASSERT(validate(), "Physics", "AABB tree validation failed after removing a leaf");
#endif
}

void AABBTree::detachLeaf(TreeNodeID leaf_id) {
	auto& leaf = m_nodes[leaf_id];
	TOAST_ASSERT(leaf.isLeaf(), "Physics", "AABB tree detach requires a leaf node");
	if (leaf_id == m_root) {
		m_root = null_node;
		return;
	}

	const TreeNodeID parent_id = leaf.parent;
	const auto& parent = m_nodes[parent_id];
	TOAST_ASSERT(parent.left == leaf_id || parent.right == leaf_id, "Physics", "AABB tree leaf must belong to its recorded parent");
	const TreeNodeID sibling_id = parent.left == leaf_id ? parent.right : parent.left;
	const TreeNodeID grandparent_id = parent.parent;

	if (grandparent_id == null_node) {
		m_root = sibling_id;
		m_nodes[sibling_id].parent = null_node;
	} else {
		auto& grandparent = m_nodes[grandparent_id];
		if (grandparent.left == parent_id) {
			grandparent.left = sibling_id;
		} else {
			TOAST_ASSERT(grandparent.right == parent_id, "Physics", "AABB tree parent must belong to its recorded grandparent");
			grandparent.right = sibling_id;
		}
		m_nodes[sibling_id].parent = grandparent_id;
	}

	leaf.parent = null_node;
	auto& detached_parent = m_nodes[parent_id];
	detached_parent.parent = null_node;
	detached_parent.left = null_node;
	detached_parent.right = null_node;
	freeNode(parent_id);

	if (grandparent_id != null_node) {
		refitAncestors(grandparent_id);
	}
}

auto AABBTree::needsUpdate(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement) const -> bool {
	const AABB& tree_bounds = m_nodes[leaf_id].bounds;
	if (not tree_bounds.contains(tight_bounds)) {
		return true;
	}

	const AABB huge = fatten(tight_bounds, displacement).expanded(4.0f * fatMargin());
	return not huge.contains(tree_bounds);
}

void AABBTree::reinsert(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement) {
	const bool is_valid_leaf = leaf_id != null_node && leaf_id < m_nodes.size() && m_nodes[leaf_id].isLeaf();
	TOAST_ASSERT(is_valid_leaf, "Physics", "AABBTree::reinsert requires an allocated leaf");
	if (not is_valid_leaf) {
		return;
	}

	detachLeaf(leaf_id);
	m_nodes[leaf_id].bounds = fatten(tight_bounds, displacement);
	insertLeaf(leaf_id);
#if defined(TOAST_VALIDATE_AABB_TREE)
	TOAST_ASSERT(validate(), "Physics", "AABB tree validation failed after updating a leaf");
#endif
}

auto AABBTree::updateLeaf(TreeNodeID leaf_id, const AABB& tight_bounds, glm::vec3 displacement) -> bool {
	const bool is_valid_leaf = leaf_id != null_node && leaf_id < m_nodes.size() && m_nodes[leaf_id].isLeaf();
	TOAST_ASSERT(is_valid_leaf, "Physics", "AABBTree::updateLeaf requires an allocated leaf");
	if (not is_valid_leaf || not needsUpdate(leaf_id, tight_bounds, displacement)) {
		return false;
	}

	reinsert(leaf_id, tight_bounds, displacement);
	return true;
}

void AABBTree::rebuild(std::span<const AABBTreeBuildLeaf> leaves, std::span<TreeNodeID> out_leaf_nodes) {
	ZoneScopedN("physics::AABBTree::Rebuild");
	ZoneValue(static_cast<uint64_t>(leaves.size()));
	TOAST_ASSERT(out_leaf_nodes.size() == leaves.size(), "Physics", "AABBTree::rebuild needs one output slot per leaf");

	constexpr auto max_leaves = static_cast<size_t>(null_node) / 2;
	if (leaves.size() > max_leaves) {
		throw std::length_error("AABBTree capacity exceeds the TreeNodeID range");
	}

	m_nodes.clear();
	m_root = null_node;
	m_free_list = null_node;
	m_active_node_count = 0;
	m_leaf_count = leaves.size();
	if (leaves.empty()) {
		return;
	}

	const size_t leaf_count = leaves.size();
	m_nodes.reserve((2 * leaf_count) - 1);
	m_nodes.resize(leaf_count);

	std::vector<TreeNodeID> order(leaf_count);
	std::vector<glm::vec3> centroids(leaf_count);
	for (size_t index = 0; index < leaf_count; ++index) {
		TreeNode& node = m_nodes[index];
		node = TreeNode {};
		node.bounds = leaves[index].bounds;
		node.shape = leaves[index].shape;
		node.height = 0;
		order[index] = static_cast<TreeNodeID>(index);
		centroids[index] = center(leaves[index].bounds);
		out_leaf_nodes[index] = static_cast<TreeNodeID>(index);
	}

	m_root = buildRange(order, centroids, 0);
	m_nodes[m_root].parent = null_node;
	m_active_node_count = m_nodes.size();
#if defined(TOAST_VALIDATE_AABB_TREE)
	TOAST_ASSERT(validate(), "Physics", "AABB tree validation failed after a rebuild");
#endif
}

auto AABBTree::buildRange(std::span<TreeNodeID> leaves, std::span<const glm::vec3> centroids, int depth) -> TreeNodeID {
	if (leaves.size() == 1) {
		return leaves.front();
	}

	glm::vec3 centroid_min = centroids[leaves.front()];
	glm::vec3 centroid_max = centroid_min;
	for (const TreeNodeID leaf : leaves) {
		centroid_min = glm::min(centroid_min, centroids[leaf]);
		centroid_max = glm::max(centroid_max, centroids[leaf]);
	}

	const glm::vec3 extent = centroid_max - centroid_min;
	const int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);    // NOLINT

	size_t split = 0;
	if (extent[axis] > 1.0e-6f && depth < k_max_sah_depth) {
		struct Bin {
			AABB bounds {};
			size_t count = 0;
		};

		std::array<Bin, k_sah_bins> bins {};

		const float axis_min = centroid_min[axis];
		const float scale = static_cast<float>(k_sah_bins) / extent[axis];
		const auto bin_of = [&](TreeNodeID leaf) {
			const int bin = static_cast<int>((centroids[leaf][axis] - axis_min) * scale);
			return std::clamp(bin, 0, k_sah_bins - 1);
		};

		for (const TreeNodeID leaf : leaves) {
			Bin& bin = bins[bin_of(leaf)];
			bin.bounds = bin.count == 0 ? m_nodes[leaf].bounds : combine(bin.bounds, m_nodes[leaf].bounds);
			++bin.count;
		}

		std::array<float, k_sah_bins> right_area {};
		std::array<size_t, k_sah_bins> right_count {};
		AABB running {};
		size_t running_count = 0;
		for (int bin = k_sah_bins - 1; bin > 0; --bin) {
			if (bins[bin].count > 0) {
				running = running_count == 0 ? bins[bin].bounds : combine(running, bins[bin].bounds);
				running_count += bins[bin].count;
			}
			right_area[bin] = running_count > 0 ? running.area() : 0.0f;
			right_count[bin] = running_count;
		}

		int best_split = -1;
		float best_cost = std::numeric_limits<float>::max();
		running_count = 0;
		for (int bin = 0; bin < k_sah_bins - 1; ++bin) {
			if (bins[bin].count > 0) {
				running = running_count == 0 ? bins[bin].bounds : combine(running, bins[bin].bounds);
				running_count += bins[bin].count;
			}
			if (running_count == 0 || right_count[bin + 1] == 0) {
				continue;
			}
			const float cost =
			    (running.area() * static_cast<float>(running_count)) + (right_area[bin + 1] * static_cast<float>(right_count[bin + 1]));
			if (cost < best_cost) {
				best_cost = cost;
				best_split = bin;
			}
		}

		if (best_split >= 0) {
			const auto middle =
			    std::partition(leaves.begin(), leaves.end(), [&](TreeNodeID leaf) { return bin_of(leaf) <= best_split; });
			split = static_cast<size_t>(middle - leaves.begin());
		}
	}

	if (split == 0 || split == leaves.size()) {
		split = leaves.size() / 2;
		const auto middle = leaves.begin() + static_cast<std::ptrdiff_t>(split);
		std::nth_element(leaves.begin(), middle, leaves.end(), [&](TreeNodeID a, TreeNodeID b) {
			return centroids[a][axis] < centroids[b][axis];
		});
	}

	const TreeNodeID left_id = buildRange(leaves.first(split), centroids, depth + 1);
	const TreeNodeID right_id = buildRange(leaves.subspan(split), centroids, depth + 1);

	const auto node_id = static_cast<TreeNodeID>(m_nodes.size());
	m_nodes.emplace_back();
	TreeNode& node = m_nodes[node_id];
	node.left = left_id;
	node.right = right_id;
	node.bounds = combine(m_nodes[left_id].bounds, m_nodes[right_id].bounds);
	node.height = 1 + std::max(m_nodes[left_id].height, m_nodes[right_id].height);
	m_nodes[left_id].parent = node_id;
	m_nodes[right_id].parent = node_id;
	return node_id;
}

auto AABBTree::query(const AABB& bounds, ShapeID ignored_shape) const -> std::vector<ShapeID> {
	// Removed due to bloating tracy shi
	// ZoneScopedN("physics::AABBTree::Query");
	// ZoneValue(static_cast<uint64_t>(ignored_shape.slot));
	std::vector<ShapeID> result;
	visitOverlaps(bounds, [&](ShapeID shape) {
		if (shape != ignored_shape) {
			result.emplace_back(shape);
		}
	});
	std::ranges::sort(result);
	// ZoneValue(static_cast<uint64_t>(result.size()));
	return result;
}

auto AABBTree::debugNodes() const -> std::vector<AABBTreeDebugNode> {
	std::vector<AABBTreeDebugNode> result;
	result.reserve(m_active_node_count);
	for (TreeNodeID node_id = 0; node_id < m_nodes.size(); ++node_id) {
		const auto& node = m_nodes[node_id];
		if (node.isAllocated()) {
			result.emplace_back(
			    AABBTreeDebugNode {
			      .bounds = node.bounds,
			      .id = node_id,
			      .height = node.height,
			      .leaf = node.isLeaf(),
			    }
			);
		}
	}
	return result;
}

void AABBTree::recalculate(TreeNodeID node_id) {
	auto& node = m_nodes[node_id];
	TOAST_ASSERT(not node.isLeaf(), "Physics", "Cannot recalculate an AABB tree leaf");
	const auto& left = m_nodes[node.left];
	const auto& right = m_nodes[node.right];
	node.bounds = combine(left.bounds, right.bounds);
	node.height = 1 + std::max(left.height, right.height);
}

void AABBTree::refitAncestors(TreeNodeID node_id) {
	while (node_id != null_node) {
		node_id = balance(node_id);
		recalculate(node_id);
		node_id = m_nodes[node_id].parent;
	}
}

auto AABBTree::balance(TreeNodeID node_id) -> TreeNodeID {
	auto& node = m_nodes[node_id];
	if (node.isLeaf() || node.height < 2) {
		return node_id;
	}

	const TreeNodeID left_id = node.left;
	const TreeNodeID right_id = node.right;
	const int balance_factor = m_nodes[right_id].height - m_nodes[left_id].height;
	if (balance_factor > 1) {
		auto& right = m_nodes[right_id];
		const TreeNodeID right_left_id = right.left;
		const TreeNodeID right_right_id = right.right;
		TOAST_ASSERT(
		    right_left_id != null_node && right_right_id != null_node, "Physics", "AABB tree right rotation requires two children"
		);

		right.left = node_id;
		right.parent = node.parent;
		node.parent = right_id;
		if (right.parent == null_node) {
			m_root = right_id;
		} else if (m_nodes[right.parent].left == node_id) {
			m_nodes[right.parent].left = right_id;
		} else {
			TOAST_ASSERT(
			    m_nodes[right.parent].right == node_id, "Physics", "AABB tree node must belong to its parent before right rotation"
			);
			m_nodes[right.parent].right = right_id;
		}

		if (m_nodes[right_left_id].height > m_nodes[right_right_id].height) {
			right.right = right_left_id;
			node.right = right_right_id;
			m_nodes[right_right_id].parent = node_id;
			m_nodes[right_left_id].parent = right_id;
		} else {
			right.right = right_right_id;
			node.right = right_left_id;
			m_nodes[right_left_id].parent = node_id;
			m_nodes[right_right_id].parent = right_id;
		}
		recalculate(node_id);
		recalculate(right_id);
		return right_id;
	}

	if (balance_factor < -1) {
		auto& left = m_nodes[left_id];
		const TreeNodeID left_left_id = left.left;
		const TreeNodeID left_right_id = left.right;
		TOAST_ASSERT(
		    left_left_id != null_node && left_right_id != null_node, "Physics", "AABB tree left rotation requires two children"
		);

		left.right = node_id;
		left.parent = node.parent;
		node.parent = left_id;
		if (left.parent == null_node) {
			m_root = left_id;
		} else if (m_nodes[left.parent].left == node_id) {
			m_nodes[left.parent].left = left_id;
		} else {
			TOAST_ASSERT(
			    m_nodes[left.parent].right == node_id, "Physics", "AABB tree node must belong to its parent before left rotation"
			);
			m_nodes[left.parent].right = left_id;
		}

		if (m_nodes[left_left_id].height > m_nodes[left_right_id].height) {
			left.left = left_left_id;
			node.left = left_right_id;
			m_nodes[left_right_id].parent = node_id;
			m_nodes[left_left_id].parent = left_id;
		} else {
			left.left = left_right_id;
			node.left = left_left_id;
			m_nodes[left_left_id].parent = node_id;
			m_nodes[left_right_id].parent = left_id;
		}
		recalculate(node_id);
		recalculate(left_id);
		return left_id;
	}

	return node_id;
}

auto AABBTree::size() const -> size_t {
	return m_active_node_count;
}

auto AABBTree::validate() const -> bool {
	ZoneScopedN("physics::AABBTree::Validate");
	ZoneValue(static_cast<uint64_t>(m_active_node_count));
	if (m_root != null_node && (m_root >= m_nodes.size() || not m_nodes[m_root].isAllocated())) {
		return false;
	}

	size_t allocated_node_count = 0;
	for (size_t node_index = 0; node_index < m_nodes.size(); ++node_index) {
		const auto& node = m_nodes[node_index];
		if (node.isAllocated()) {
			++allocated_node_count;

			const auto valid_allocated_index = [this](TreeNodeID node_id) {
				return node_id == null_node || (node_id < m_nodes.size() && m_nodes[node_id].isAllocated());
			};
			if (not valid_allocated_index(node.parent) || not valid_allocated_index(node.left) ||
			    not valid_allocated_index(node.right) || node.next_free != null_node) {
				return false;
			}

			const bool has_left = node.left != null_node;
			const bool has_right = node.right != null_node;
			if (has_left != has_right) {
				return false;
			}

			if (not has_left) {
				if (node.height != 0) {
					return false;
				}
			} else {
				const auto& left = m_nodes[node.left];
				const auto& right = m_nodes[node.right];
				const auto combined_bounds = combine(left.bounds, right.bounds);
				const bool bounds_match = node.bounds.min == combined_bounds.min && node.bounds.max == combined_bounds.max;
				if (left.parent != node_index || right.parent != node_index || not bounds_match ||
				    node.height != 1 + std::max(left.height, right.height)) {
					return false;
				}
			}

			if (node.parent == null_node) {
				if (node_index != m_root) {
					return false;
				}
			} else {
				const auto& parent = m_nodes[node.parent];
				if (parent.left != node_index && parent.right != node_index) {
					return false;
				}
			}
		} else if (node.height != -1 || node.parent != null_node || node.left != null_node || node.right != null_node) {
			return false;
		}
	}

	if (allocated_node_count != m_active_node_count) {
		return false;
	}
	if (m_active_node_count != 0 && m_active_node_count != (2 * m_leaf_count) - 1) {
		return false;
	}
	if ((m_root == null_node) != (allocated_node_count == 0)) {
		return false;
	}

	if (m_root != null_node) {
		std::vector<bool> visited_nodes(m_nodes.size(), false);
		std::vector<TreeNodeID> stack {m_root};
		size_t visited_node_count = 0;
		while (not stack.empty()) {
			const TreeNodeID node_id = stack.back();
			stack.pop_back();
			if (visited_nodes[node_id]) {
				return false;
			}
			visited_nodes[node_id] = true;
			++visited_node_count;

			const auto& node = m_nodes[node_id];
			if (not node.isLeaf()) {
				stack.emplace_back(node.left);
				stack.emplace_back(node.right);
			}
		}
		if (visited_node_count != allocated_node_count) {
			return false;
		}
	}

	std::vector<bool> free_nodes(m_nodes.size(), false);
	for (TreeNodeID node_id = m_free_list; node_id != null_node; node_id = m_nodes[node_id].next_free) {
		if (node_id >= m_nodes.size() || m_nodes[node_id].isAllocated() || free_nodes[node_id]) {
			return false;
		}
		free_nodes[node_id] = true;
	}

	for (size_t node_id = 0; node_id < m_nodes.size(); ++node_id) {
		if (not m_nodes[node_id].isAllocated() && not free_nodes[node_id]) {
			return false;
		}
	}

	return true;
}

auto AABB::overlaps(const AABB& other) const -> bool {
	bool intersects_x = min.x <= other.max.x && max.x >= other.min.x;
	bool intersects_y = min.y <= other.max.y && max.y >= other.min.y;
	bool intersects_z = min.z <= other.max.z && max.z >= other.min.z;
	return intersects_x && intersects_y && intersects_z;
}

auto AABB::contains(const AABB& other) const -> bool {
	bool contains_x = min.x <= other.min.x && max.x >= other.max.x;
	bool contains_y = min.y <= other.min.y && max.y >= other.max.y;
	bool contains_z = min.z <= other.min.z && max.z >= other.max.z;
	return contains_x && contains_y && contains_z;
}

auto AABB::expanded(float amount) const -> AABB {
	return {.min = min - amount, .max = max + amount};
}

auto AABB::area() const -> float {
	auto d = max - min;
	return 2 * ((d.x * d.y) + (d.x * d.z) + (d.y * d.z));
}

auto AABB::intersectRay(const glm::vec3& origin, const glm::vec3& inv_dir) const -> std::optional<RayHit> {
	float t_min = -std::numeric_limits<float>::infinity();
	float t_max = std::numeric_limits<float>::infinity();

	for (int axis = 0; axis < 3; ++axis) {
		if (std::isinf(inv_dir[axis])) {
			if (origin[axis] < min[axis] || origin[axis] > max[axis]) {
				return std::nullopt;
			}
			continue;
		}

		float t1 = (min[axis] - origin[axis]) * inv_dir[axis];
		float t2 = (max[axis] - origin[axis]) * inv_dir[axis];
		if (t1 > t2) {
			std::swap(t1, t2);
		}
		t_min = std::max(t_min, t1);
		t_max = std::min(t_max, t2);
		if (t_min > t_max) {
			return std::nullopt;
		}
	}

	return RayHit {.t_min = t_min, .t_max = t_max};
}

auto combine(const AABB& lhs, const AABB& rhs) -> AABB {
	glm::vec3 min = glm::min(lhs.min, rhs.min);
	glm::vec3 max = glm::max(lhs.max, rhs.max);

	return {.min = min, .max = max};
}

auto operator+(const AABB& lhs, const AABB& rhs) -> AABB {
	return combine(lhs, rhs);
}
}
