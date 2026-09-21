#include "broad_phase.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <glm/gtc/quaternion.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>

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

	for (size_t shape_index = begin; shape_index < end; ++shape_index) {
		const ShapeSlot& slot = world.shapes[shape_index];
		const ShapeID shape_id {.slot = static_cast<uint32_t>(shape_index), .generation = slot.generation};
		const Body* body = slot.occupied ? world.body(slot.shape.owner) : nullptr;
		const bool active = body && body->enabled && slot.shape.enabled;

		updates.emplace_back(
		    ShapeBoundsUpdate {
		      .shape = shape_id,
		      .bounds = active ? worldShapeBounds(*body, slot.shape) : AABB {},
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

	constexpr size_t minimum_bounds_per_job = 32;
	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), 1ull);
	const size_t maximum_job_count = worker_count * 3;
	const size_t job_count =
	    world.shapes.empty() ? 0 : std::min(maximum_job_count, std::max(world.shapes.size() / minimum_bounds_per_job, size_t {1}));

	std::vector<std::future<std::vector<ShapeBoundsUpdate>>> futures;
	futures.reserve(job_count);

	for (size_t job_index = 0; job_index < job_count; ++job_index) {
		const size_t begin = job_index * world.shapes.size() / job_count;
		const size_t end = (job_index + 1) * world.shapes.size() / job_count;

		futures.emplace_back(toast::ThreadPool::push([this, world, begin, end] {
			ZoneScopedN("physics::AABBBatch");
			ZoneValue(static_cast<uint64_t>(end - begin));
			return calculateBounds(world, begin, end);
		}));
	}
	m_stats.bounds_jobs = futures.size();

	std::vector<ShapeBoundsUpdate> bounds;
	bounds.reserve(world.shapes.size());

	{
		ZoneScopedNC("physics::AABBAwait", 0x202020);
		for (auto& future : futures) {
			std::vector<ShapeBoundsUpdate> local = future.get();
			bounds.insert_range(bounds.end(), std::move(local));
		}
	}

	{
		ZoneScopedN("physics::UpdateAABBTree");
		while (m_shape_leaves.size() > world.shapes.size()) {
			const auto& entry = m_shape_leaves.back();
			if (entry.node != null_node) {
				m_tree.remove(entry.node);
				++m_stats.removed_leaves;
			}
			m_shape_leaves.pop_back();
		}
		m_shape_leaves.resize(world.shapes.size());

		for (size_t shape_index = 0; shape_index < world.shapes.size(); ++shape_index) {
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

			if (entry.node == null_node) {
				entry.node = m_tree.insert(update.shape, update.bounds);
				entry.generation = update.shape.generation;
				++m_stats.inserted_leaves;
			} else if (isShapeAsleep(world, update.shape)) {
				++m_stats.skipped_refits;
			} else {
				m_stats.reinserted_leaves += m_tree.updateLeaf(entry.node, update.bounds);
			}
		}
	}

	std::vector<BroadPhasePair> pairs;
	{
		ZoneScopedN("physics::QueryAABBTree");
		for (size_t shape_index = 0; shape_index < m_shape_leaves.size(); ++shape_index) {
			const auto& entry = m_shape_leaves[shape_index];
			if (entry.node == null_node) {
				continue;
			}

			const ShapeBoundsUpdate& update = bounds[shape_index];
			if (not update.active) {
				continue;
			}

			if (isShapeAsleep(world, update.shape)) {
				++m_stats.skipped_self_queries;
				continue;
			}

			const std::vector<ShapeID> query_results = m_tree.query(update.bounds, update.shape);
			++m_stats.queries;
			m_stats.query_hits += query_results.size();
			for (ShapeID candidate : query_results) {
				if (auto pair = testPair(world, update.shape, candidate)) {
					pairs.emplace_back(*pair);
				}
			}
		}
	}

	{
		ZoneScopedN("physics::SortAndDeduplicatePairs");
		ZoneValue(static_cast<uint64_t>(pairs.size()));
		m_stats.pair_records = pairs.size();
		std::ranges::sort(pairs);
		pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
	}
	m_stats.candidate_pairs = pairs.size();
	m_stats.duplicate_pairs = m_stats.pair_records - m_stats.candidate_pairs;
	m_stats.tree_nodes = m_tree.size();
	ZoneValue(static_cast<uint64_t>(pairs.size()));
	return pairs;
}

auto BroadPhase::queryBounds(const AABB& bounds) const -> std::vector<ShapeID> {
	ZoneScopedN("physics::QueryBounds");
	return m_tree.query(bounds);
}

auto BroadPhase::debugNodes() const -> std::vector<AABBTreeDebugNode> {
	return m_tree.debugNodes();
}

auto BroadPhase::stats() const -> const BroadPhaseStats& {
	return m_stats;
}

auto BroadPhase::testPair(CollisionWorldView world, ShapeID shape_a_id, ShapeID shape_b_id) -> std::optional<BroadPhasePair> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(shape_a_id.slot) << 32) | static_cast<uint64_t>(shape_b_id.slot));

	const Shape* shape_a = world.shape(shape_a_id);
	const Shape* shape_b = world.shape(shape_b_id);
	if (not shape_a || not shape_b || shape_a_id == shape_b_id) {
		++m_stats.rejected_invalid_shapes;
		return std::nullopt;
	}
	if (not shape_a->enabled || not shape_b->enabled) {
		++m_stats.rejected_disabled_shapes;
		return std::nullopt;
	}

	if (shape_a->owner == shape_b->owner) {
		++m_stats.rejected_same_body;
		return std::nullopt;
	}

	const Body* body_a = world.body(shape_a->owner);
	const Body* body_b = world.body(shape_b->owner);
	if (not body_a || not body_b || not body_a->enabled || not body_b->enabled) {
		++m_stats.rejected_invalid_bodies;
		return std::nullopt;
	}

	if (body_a->inverse_mass == 0.0f && body_b->inverse_mass == 0.0f) {
		++m_stats.rejected_immovable_bodies;
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
