/**
 * @file broad_phase.hpp
 * @brief Generates potentially colliding shape pairs
 */

#pragma once

#include "aabb_tree.hpp"
#include "collision.hpp"
#include "collision_world.hpp"

#include <optional>
#include <vector>

namespace physics {

class BroadPhase {
public:
	[[nodiscard]]
	auto findPairs(CollisionWorldView world) -> std::vector<BroadPhasePair>;

	[[nodiscard]]
	auto debugNodes() const -> std::vector<AABBTreeDebugNode>;

private:
	struct ShapeLeaf {
		TreeNodeID node = null_node;
		uint32_t generation = 0;
	};

	[[nodiscard]]
	auto testPair(CollisionWorldView world, ShapeID shape_a, ShapeID shape_b) const -> std::optional<BroadPhasePair>;

	AABBTree m_tree;
	std::vector<ShapeLeaf> m_shape_leaves;
};

}
