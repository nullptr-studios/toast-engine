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

struct ShapeBoundsUpdate {
	ShapeID shape;
	AABB bounds;
	bool active = false;
};

struct BroadPhaseStats {
	size_t input_shapes = 0;
	size_t active_shapes = 0;
	size_t bounds_jobs = 0;
	size_t inserted_leaves = 0;
	size_t reinserted_leaves = 0;
	size_t removed_leaves = 0;
	size_t queries = 0;
	size_t query_hits = 0;
	size_t pair_records = 0;
	size_t candidate_pairs = 0;
	size_t duplicate_pairs = 0;
	size_t rejected_invalid_shapes = 0;
	size_t rejected_disabled_shapes = 0;
	size_t rejected_same_body = 0;
	size_t rejected_invalid_bodies = 0;
	size_t rejected_immovable_bodies = 0;
	size_t tree_nodes = 0;
};

class BroadPhase {
public:
	[[nodiscard]]
	auto calculateBounds(CollisionWorldView world, size_t begin, size_t end) const -> std::vector<ShapeBoundsUpdate>;

	[[nodiscard]]
	auto findPairs(CollisionWorldView world) -> std::vector<BroadPhasePair>;

	[[nodiscard]]
	auto debugNodes() const -> std::vector<AABBTreeDebugNode>;
	[[nodiscard]]
	auto stats() const -> const BroadPhaseStats&;

private:
	struct ShapeLeaf {
		TreeNodeID node = null_node;
		uint32_t generation = 0;
	};

	[[nodiscard]]
	auto testPair(CollisionWorldView world, ShapeID shape_a, ShapeID shape_b) -> std::optional<BroadPhasePair>;

	AABBTree m_tree;
	std::vector<ShapeLeaf> m_shape_leaves;
	BroadPhaseStats m_stats;
};

}
