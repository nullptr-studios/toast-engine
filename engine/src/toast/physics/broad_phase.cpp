#include "broad_phase.hpp"

#include "physics_settings.hpp"

#include <future>

namespace physics {

namespace {

auto normalized(const glm::quat& rotation) -> glm::quat {
	const float length_squared = glm::dot(rotation, rotation);
	return std::isfinite(length_squared) && length_squared > 1.0e-10f ? rotation / std::sqrt(length_squared)
	                                                                  : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

auto isShapeAsleep(CollisionWorldView world, ShapeID shape_id) -> bool {
	const Shape* shape = world.shape(shape_id);
	if (shape == nullptr) {
		return false;
	}
	const Body* body = world.body(shape->owner);
	return body != nullptr && body->type == BodyType::dynamic_body && body->enabled && not body->awake;
}

auto isActiveQuerier(CollisionWorldView world, ShapeID shape_id) -> bool {
	const Shape* shape = world.shape(shape_id);
	if (shape == nullptr) {
		return false;
	}
	const Body* body = world.body(shape->owner);
	return body != nullptr && body->type == BodyType::dynamic_body && body->enabled && body->awake;
}

}

auto worldShapeBounds(const Body& body, const Shape& shape) -> AABB {
	switch (shape.type) {
		case ShapeType::sphere: {
			const glm::vec3 center = body.position + body.rotation * shape.sphere.local_center;
			const glm::vec3 extent {shape.sphere.radius};
			return {.min = center - extent, .max = center + extent};
		}
		case ShapeType::capsule: {
			const glm::vec3 center = body.position + body.rotation * shape.capsule.local_center;
			const glm::quat rotation = normalized(body.rotation * shape.capsule.local_rotation);
			const glm::vec3 axis = rotation * glm::vec3 {0.0f, 0.0f, 1.0f};
			const float shaft_half_length = (0.5f * shape.capsule.height) - shape.capsule.radius;
			const glm::vec3 point_a = center - axis * shaft_half_length;
			const glm::vec3 point_b = center + axis * shaft_half_length;
			const glm::vec3 extent {shape.capsule.radius};
			return {.min = glm::min(point_a, point_b) - extent, .max = glm::max(point_a, point_b) + extent};
		}
		case ShapeType::box: {
			const glm::vec3 center = body.position + body.rotation * shape.box.local_center;
			const glm::mat3 rotation = glm::mat3_cast(normalized(body.rotation * shape.box.local_rotation));
			const glm::vec3 half_extents = shape.box.size * 0.5f;
			const glm::vec3 world_extents = glm::abs(rotation[0]) * half_extents.x + glm::abs(rotation[1]) * half_extents.y +
			                                glm::abs(rotation[2]) * half_extents.z;
			return {.min = center - world_extents, .max = center + world_extents};
		}
		case ShapeType::voxel: {
			const AABB& bounds = shape.voxel.local_bounds;
			const glm::vec3 local_center = (bounds.min + bounds.max) * 0.5f;
			const glm::vec3 half_extents = (bounds.max - bounds.min) * 0.5f;
			const glm::quat rotation = normalized(body.rotation * shape.voxel.local_rotation);
			const glm::vec3 world_center = body.position + body.rotation * shape.voxel.local_center + rotation * local_center;
			const glm::mat3 r = glm::mat3_cast(rotation);
			const glm::vec3 world_extents =
			    glm::abs(r[0]) * half_extents.x + glm::abs(r[1]) * half_extents.y + glm::abs(r[2]) * half_extents.z;

			return {.min = world_center - world_extents, .max = world_center + world_extents};
		}
	}

	return {};
}

auto BroadPhase::calculateBounds(CollisionWorldView world, size_t begin, size_t end) const -> std::vector<ShapeBoundsUpdate> {
	ZoneScopedN("physics::CalculateBounds");

	std::vector<ShapeBoundsUpdate> updates;
	updates.reserve(end - begin);

	const float lookahead = static_cast<float>(tunables().fixedDelta()) * tunables().broadphase_velocity_multiplier;

	for (size_t shape_index = begin; shape_index < end; ++shape_index) {
		const ShapeSlot& slot = world.shapes[shape_index];
		const ShapeID shape_id {.slot = static_cast<uint32_t>(shape_index), .generation = slot.generation};
		const Body* body = slot.occupied ? world.body(slot.shape.owner) : nullptr;
		const bool active = body && body->enabled && slot.shape.enabled;
		const bool moves = active && body->type != BodyType::static_body;

		updates.emplace_back(
		    ShapeBoundsUpdate {
		      .shape = shape_id,
		      .bounds = active ? worldShapeBounds(*body, slot.shape) : AABB {},
		      .displacement = moves ? body->linear_velocity * lookahead : glm::vec3 {},
		      .active = active,
		    }
		);
	}

	ZoneValue(static_cast<uint64_t>(updates.size()));
	return updates;
}

auto BroadPhase::findPairs(CollisionWorldView world) -> std::vector<BroadPhasePair> {
	ZoneScopedN("physics::BroadPhase");
	ZoneValue(static_cast<uint64_t>(world.shapes.size()));
	m_stats = {.input_shapes = world.shapes.size()};

	const size_t minimum_bounds_per_job = tunables().min_bounds_per_job;
	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), static_cast<size_t>(1));
	const size_t maximum_job_count = worker_count * 3;
	const size_t job_count =
	    world.shapes.empty() ? 0 : std::min(maximum_job_count, std::max(world.shapes.size() / minimum_bounds_per_job, size_t {1}));

	std::vector<std::future<std::vector<ShapeBoundsUpdate>>> futures;
	futures.reserve(job_count);

	for (size_t job_index = 0; job_index < job_count; ++job_index) {
		const size_t begin = job_index * world.shapes.size() / job_count;
		const size_t end = (job_index + 1) * world.shapes.size() / job_count;

		futures.emplace_back(toast::ThreadPool::push([this, world, begin, end] {
			// ZoneScopedN("physics::AABBBatch");
			// ZoneValue(static_cast<uint64_t>(end - begin));
			return calculateBounds(world, begin, end);
		}));
	}
	m_stats.bounds_jobs = futures.size();

	std::vector<ShapeBoundsUpdate> bounds;
	bounds.reserve(world.shapes.size());

	{
		// ZoneScopedNC("physics::AABBAwait", 0x202020);
		for (auto& future : futures) {
			std::vector<ShapeBoundsUpdate> local = future.get();
			bounds.insert_range(bounds.end(), std::move(local));
		}
	}

	updateTree(world, bounds);

	std::vector<BroadPhasePair> pairs;
	{
		ZoneScopedN("physics::QueryAABBTree");

		// Tree is done mutating so concurrent queries are safe each job writes its own stats/pairs only
		struct QueryChunk {
			std::vector<BroadPhasePair> pairs;
			BroadPhaseStats stats;
		};

		const size_t minimum_shapes_per_job = tunables().min_shapes_per_query_job;
		const size_t worker_count = std::max(toast::ThreadPool::workerCount(), static_cast<size_t>(1));
		const size_t maximum_job_count = worker_count * 3;
		const size_t querier_count = m_queriers.size();
		const size_t job_count =
		    querier_count == 0 ? 0 : std::min(maximum_job_count, std::max(querier_count / minimum_shapes_per_job, size_t {1}));

		const std::span<const ShapeBoundsUpdate> bounds_view {bounds};
		std::vector<std::future<QueryChunk>> futures;
		futures.reserve(job_count);

		for (size_t job_index = 0; job_index < job_count; ++job_index) {
			const size_t begin = job_index * querier_count / job_count;
			const size_t end = (job_index + 1) * querier_count / job_count;

			futures.emplace_back(toast::ThreadPool::push([this, world, bounds_view, begin, end] {
				ZoneScopedN("physics::QueryAABBTreeBatch");
				QueryChunk chunk;
				for (size_t querier = begin; querier < end; ++querier) {
					const uint32_t shape_index = m_queriers[querier];
					const ShapeBoundsUpdate& update = bounds_view[shape_index];
					const AABB& own_fat_bounds = m_tree.leafBounds(m_shape_leaves[shape_index].node);

					++chunk.stats.queries;
					chunk.stats.query_nodes_visited += m_tree.visitOverlaps(update.bounds, [&](ShapeID candidate) {
						if (candidate == update.shape) {
							return;
						}
						++chunk.stats.query_hits;
						if (candidate.slot < shape_index && m_is_querier[candidate.slot] &&
						    bounds_view[candidate.slot].bounds.overlaps(own_fat_bounds)) {
							++chunk.stats.skipped_mirror_hits;
							return;
						}

						if (auto pair = testPair(world, update.shape, candidate, chunk.stats)) {
							chunk.pairs.emplace_back(*pair);
						}
					});
				}
				return chunk;
			}));
		}

		for (auto& future : futures) {
			QueryChunk chunk = future.get();
			m_stats.queries += chunk.stats.queries;
			m_stats.query_nodes_visited += chunk.stats.query_nodes_visited;
			m_stats.query_hits += chunk.stats.query_hits;
			m_stats.skipped_mirror_hits += chunk.stats.skipped_mirror_hits;
			m_stats.rejected_invalid_shapes += chunk.stats.rejected_invalid_shapes;
			m_stats.rejected_disabled_shapes += chunk.stats.rejected_disabled_shapes;
			m_stats.rejected_same_body += chunk.stats.rejected_same_body;
			m_stats.rejected_invalid_bodies += chunk.stats.rejected_invalid_bodies;
			m_stats.rejected_immovable_bodies += chunk.stats.rejected_immovable_bodies;
			m_stats.rejected_layers += chunk.stats.rejected_layers;
			m_stats.rejected_joint_excluded += chunk.stats.rejected_joint_excluded;
			pairs.insert_range(pairs.end(), std::move(chunk.pairs));
		}
	}

	{
		// ZoneScopedN("physics::SortAndDeduplicatePairs");
		// ZoneValue(static_cast<uint64_t>(pairs.size()));
		m_stats.pair_records = pairs.size();
		std::ranges::sort(pairs);
		pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
	}
	m_stats.candidate_pairs = pairs.size();
	m_stats.duplicate_pairs = m_stats.pair_records - m_stats.candidate_pairs;
	m_stats.tree_nodes = m_tree.size();
	m_stats.tree_height = static_cast<size_t>(m_tree.height());
	// ZoneValue(static_cast<uint64_t>(pairs.size()));
	return pairs;
}

void BroadPhase::updateTree(CollisionWorldView world, std::span<const ShapeBoundsUpdate> bounds) {
	ZoneScopedN("physics::UpdateAABBTree");
	const size_t shape_count = world.shapes.size();

	while (m_shape_leaves.size() > shape_count) {
		const auto& entry = m_shape_leaves.back();
		if (entry.node != null_node) {
			m_tree.remove(entry.node);
			++m_stats.removed_leaves;
		}
		m_shape_leaves.pop_back();
	}
	m_shape_leaves.resize(shape_count);

	m_pending_leaves.clear();
	m_queriers.clear();
	m_is_querier.assign(shape_count, 0);
	size_t new_leaves = 0;

	// removals happen right away
	// inserts and moves are only collected
	for (size_t shape_index = 0; shape_index < shape_count; ++shape_index) {
		const ShapeBoundsUpdate& update = bounds[shape_index];
		auto& entry = m_shape_leaves[shape_index];
		m_stats.active_shapes += update.active;

		if (entry.node != null_node && (not update.active || entry.generation != update.shape.generation)) {
			m_tree.remove(entry.node);
			++m_stats.removed_leaves;
			entry = {};
		}

		if (not update.active) {
			continue;
		}

		if (isActiveQuerier(world, update.shape)) {
			m_queriers.emplace_back(static_cast<uint32_t>(shape_index));
			m_is_querier[shape_index] = 1;
		} else {
			++m_stats.skipped_self_queries;
		}

		if (entry.node == null_node) {
			m_pending_leaves.emplace_back(static_cast<uint32_t>(shape_index));
			++new_leaves;
		} else if (isShapeAsleep(world, update.shape)) {
			++m_stats.skipped_refits;
		} else if (m_tree.needsUpdate(entry.node, update.bounds, update.displacement)) {
			m_pending_leaves.emplace_back(static_cast<uint32_t>(shape_index));
		}
	}

	m_stats.inserted_leaves += new_leaves;
	m_stats.reinserted_leaves += m_pending_leaves.size() - new_leaves;

	// on an explosion a rebuild is cheaper than a lot of reinsertions
	// also the tree is better
	const size_t leaf_count = m_tree.leafCount() + new_leaves;
	const bool rebuild =
	    not m_pending_leaves.empty() &&
	    static_cast<float>(m_pending_leaves.size()) >= tunables().broadphase_rebuild_fraction * static_cast<float>(leaf_count);
	ZoneValue(static_cast<uint64_t>(m_pending_leaves.size()));

	if (not rebuild) {
		for (const uint32_t shape_index : m_pending_leaves) {
			const ShapeBoundsUpdate& update = bounds[shape_index];
			auto& entry = m_shape_leaves[shape_index];
			if (entry.node == null_node) {
				entry.node = m_tree.insert(update.shape, update.bounds, update.displacement);
				entry.generation = update.shape.generation;
			} else {
				m_tree.reinsert(entry.node, update.bounds, update.displacement);
			}
		}
		return;
	}

	++m_stats.rebuilds;
	m_build_leaves.clear();
	m_build_shapes.clear();
	m_build_leaves.reserve(leaf_count);
	m_build_shapes.reserve(leaf_count);

	// m_pending_leaves is in slot order
	size_t pending_cursor = 0;
	for (size_t shape_index = 0; shape_index < shape_count; ++shape_index) {
		const ShapeBoundsUpdate& update = bounds[shape_index];
		if (not update.active) {
			continue;
		}

		const bool pending = pending_cursor < m_pending_leaves.size() && m_pending_leaves[pending_cursor] == shape_index;
		pending_cursor += pending;

		const auto& entry = m_shape_leaves[shape_index];
		m_build_leaves.emplace_back(
		    AABBTreeBuildLeaf {
		      .shape = update.shape,
		      .bounds = pending ? AABBTree::fatten(update.bounds, update.displacement) : m_tree.leafBounds(entry.node),
		    }
		);
		m_build_shapes.emplace_back(static_cast<uint32_t>(shape_index));
	}

	m_build_nodes.resize(m_build_leaves.size());
	m_tree.rebuild(m_build_leaves, m_build_nodes);
	for (size_t index = 0; index < m_build_shapes.size(); ++index) {
		auto& entry = m_shape_leaves[m_build_shapes[index]];
		entry.node = m_build_nodes[index];
		entry.generation = bounds[m_build_shapes[index]].shape.generation;
	}
}

auto BroadPhase::queryBounds(const AABB& bounds) const -> std::vector<ShapeID> {
	return m_tree.query(bounds);
}

auto BroadPhase::debugNodes() const -> std::vector<AABBTreeDebugNode> {
	return m_tree.debugNodes();
}

auto BroadPhase::stats() const -> const BroadPhaseStats& {
	return m_stats;
}

auto BroadPhase::testPair(CollisionWorldView world, ShapeID shape_a_id, ShapeID shape_b_id, BroadPhaseStats& stats)
    -> std::optional<BroadPhasePair> {
	// ZoneScoped;
	// ZoneValue((static_cast<uint64_t>(shape_a_id.slot) << 32) | static_cast<uint64_t>(shape_b_id.slot));

	const Shape* shape_a = world.shape(shape_a_id);
	const Shape* shape_b = world.shape(shape_b_id);
	if (not shape_a || not shape_b || shape_a_id == shape_b_id) {
		++stats.rejected_invalid_shapes;
		return std::nullopt;
	}
	if (not shape_a->enabled || not shape_b->enabled) {
		++stats.rejected_disabled_shapes;
		return std::nullopt;
	}

	if (shape_a->owner == shape_b->owner) {
		++stats.rejected_same_body;
		return std::nullopt;
	}

	if (world.pairExcluded(shape_a->owner, shape_b->owner)) {
		++stats.rejected_joint_excluded;
		return std::nullopt;
	}

	const Body* body_a = world.body(shape_a->owner);
	const Body* body_b = world.body(shape_b->owner);
	if (not body_a || not body_b || not body_a->enabled || not body_b->enabled) {
		++stats.rejected_invalid_bodies;
		return std::nullopt;
	}

	if (body_a->inverse_mass == 0.0f && body_b->inverse_mass == 0.0f) {
		++stats.rejected_immovable_bodies;
		return std::nullopt;
	}

	if (not layersCollide(body_a->layers, body_b->layers)) {
		++stats.rejected_layers;
		return std::nullopt;
	}

	return canonicalPair(
	    BodyShapeKey {
	      .body = shape_a->owner,
	      .shape = shape_a_id,
	    },
	    BodyShapeKey {
	      .body = shape_b->owner,
	      .shape = shape_b_id,
	    }
	);
}
}
