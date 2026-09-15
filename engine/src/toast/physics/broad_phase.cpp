#include "broad_phase.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {

auto normalized(const glm::quat& rotation) -> glm::quat {
	const float length_squared = glm::dot(rotation, rotation);
	return std::isfinite(length_squared) && length_squared > 1.0e-10f
	           ? rotation / std::sqrt(length_squared)
	           : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

auto shapeBounds(const Body& body, const Shape& shape) -> AABB {
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
			const float shaft_half_length = 0.5f * shape.capsule.height - shape.capsule.radius;
			const glm::vec3 point_a = center - axis * shaft_half_length;
			const glm::vec3 point_b = center + axis * shaft_half_length;
			const glm::vec3 extent {shape.capsule.radius};
			return {.min = glm::min(point_a, point_b) - extent, .max = glm::max(point_a, point_b) + extent};
		}
		case ShapeType::box: {
			const glm::vec3 center = body.position + body.rotation * shape.box.local_center;
			const glm::mat3 rotation = glm::mat3_cast(normalized(body.rotation * shape.box.local_rotation));
			const glm::vec3 half_extents = shape.box.size * 0.5f;
			const glm::vec3 world_extents =
			    glm::abs(rotation[0]) * half_extents.x + glm::abs(rotation[1]) * half_extents.y
			    + glm::abs(rotation[2]) * half_extents.z;
			return {.min = center - world_extents, .max = center + world_extents};
		}
	}

	return {};
}

}

auto BroadPhase::findPairs(CollisionWorldView world) -> std::vector<BroadPhasePair> {
	ZoneScoped;

	while (m_shape_leaves.size() > world.shapes.size()) {
		const auto& entry = m_shape_leaves.back();
		if (entry.node != null_node) {
			m_tree.remove(entry.node);
		}
		m_shape_leaves.pop_back();
	}
	m_shape_leaves.resize(world.shapes.size());

	for (size_t shape_index = 0; shape_index < world.shapes.size(); ++shape_index) {
		const auto& slot = world.shapes[shape_index];
		auto& entry = m_shape_leaves[shape_index];
		const ShapeID shape_id {.slot = static_cast<uint32_t>(shape_index), .generation = slot.generation};
		const Body* body = slot.occupied ? world.body(slot.shape.owner) : nullptr;
		const bool active = body && body->enabled && slot.shape.enabled;

		if (entry.node != null_node && (not active || entry.generation != slot.generation)) {
			m_tree.remove(entry.node);
			entry = {};
		}

		if (not active) {
			continue;
		}

		const AABB tight_bounds = shapeBounds(*body, slot.shape);
		if (entry.node == null_node) {
			entry.node = m_tree.insert(shape_id, tight_bounds);
			entry.generation = slot.generation;
		} else {
			(void)m_tree.updateLeaf(entry.node, tight_bounds);
		}
	}

	std::vector<BroadPhasePair> pairs;
	for (size_t shape_index = 0; shape_index < m_shape_leaves.size(); ++shape_index) {
		const auto& entry = m_shape_leaves[shape_index];
		if (entry.node == null_node) {
			continue;
		}

		const ShapeID shape_id {.slot = static_cast<uint32_t>(shape_index), .generation = entry.generation};
		const Shape* shape = world.shape(shape_id);
		const Body* body = shape ? world.body(shape->owner) : nullptr;
		if (not shape || not body) {
			continue;
		}

		for (ShapeID candidate : m_tree.query(shapeBounds(*body, *shape), shape_id)) {
			if (auto pair = testPair(world, shape_id, candidate)) {
				pairs.emplace_back(*pair);
			}
		}
	}

	std::ranges::sort(pairs);
	pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
	ZoneValue(static_cast<uint64_t>(pairs.size()));
	return pairs;
}

auto BroadPhase::debugNodes() const -> std::vector<AABBTreeDebugNode> {
	return m_tree.debugNodes();
}

auto BroadPhase::testPair(CollisionWorldView world, ShapeID shape_a_id, ShapeID shape_b_id) const
    -> std::optional<BroadPhasePair> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(shape_a_id.slot) << 32) | static_cast<uint64_t>(shape_b_id.slot));

	const Shape* shape_a = world.shape(shape_a_id);
	const Shape* shape_b = world.shape(shape_b_id);
	if (not shape_a || not shape_b || not shape_a->enabled || not shape_b->enabled || shape_a_id == shape_b_id) {
		return std::nullopt;
	}

	if (shape_a->owner == shape_b->owner) {
		return std::nullopt;
	}

	const Body* body_a = world.body(shape_a->owner);
	const Body* body_b = world.body(shape_b->owner);
	if (not body_a || not body_b || not body_a->enabled || not body_b->enabled) {
		return std::nullopt;
	}

	if (body_a->inverse_mass == 0.0f && body_b->inverse_mass == 0.0f) {
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
