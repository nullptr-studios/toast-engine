#include "broad_phase.hpp"
#include "manifold.hpp"
#include "physics_settings.hpp"
#include "simulator.hpp"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {

constexpr float k_approach_epsilon = 1.0e-4f;
constexpr float k_kinematic_wake_margin = 0.05f;

auto capsuleProbe(BodyID owner, const CapsuleShape& capsule) -> Shape {
	return Shape {.owner = owner, .type = ShapeType::capsule, .capsule = capsule};
}

auto probeBody(const glm::vec3& position, const glm::quat& rotation) -> Body {
	return Body {.type = BodyType::kinematic_body, .position = position, .rotation = rotation};
}

}

auto Simulator::current() -> Simulator* {
	return instance;
}

auto Simulator::queryCandidates(BodyID ignored, const AABB& bounds) const -> std::vector<ShapeID> {
	std::vector<ShapeID> result;
	// the tree is refit once per step so bodies may have drifted up to one margin since
	for (const ShapeID shape_id : m_broad_phase.queryBounds(bounds.expanded(tunables().broadphase_fat_margin))) {
		const Shape* shape = tryGetShape(shape_id);
		if (shape == nullptr || not shape->enabled || shape->owner == ignored) {
			continue;
		}
		const Body* body = tryGetBody(shape->owner);
		if (body == nullptr || not body->enabled) {
			continue;
		}
		result.push_back(shape_id);
	}
	return result;
}

void Simulator::collideCapsuleProbe(
    const Body& probe_body, const Shape& probe_shape, std::span<const ShapeID> candidates, std::vector<QueryContact>& contacts
) const {
	std::vector<Manifold> manifolds;
	for (const ShapeID shape_id : candidates) {
		const Shape* shape = tryGetShape(shape_id);
		const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
		if (body == nullptr) {
			continue;
		}

		const BroadPhasePair pair {
		  .a = {.body = probe_shape.owner},
        .b = {.body = shape->owner, .shape = shape_id}
		};
		const CollisionElement probe {probe_shape, probe_body};
		const CollisionElement other {*shape, *body};

		// every manifold ends up with the probe as A so the normal points from the probe into the obstacle
		manifolds.clear();
		switch (shape->type) {
			case ShapeType::sphere:
				if (auto manifold = collideSphereCapsule(pair, other, probe)) {
					manifold->normal = -manifold->normal;
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::box:
				if (auto manifold = collideCapsuleBox(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::capsule:
				if (auto manifold = collideCapsules(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::voxel:
				if (const VoxelShapeData* data = tryGetVoxelData(shape->voxel.data); data != nullptr && data->volume != nullptr) {
					collideCapsuleVoxel(pair, probe, other, *data, manifolds);
				}
				break;
		}

		for (const Manifold& manifold : manifolds) {
			if (manifold.contact_count == 0) {
				continue;
			}
			const auto points = std::span {manifold.contacts}.first(manifold.contact_count);
			const ContactPoint& deepest = *std::ranges::max_element(points, {}, &ContactPoint::penetration);
			contacts.push_back(
			    QueryContact {
			      .body = shape->owner,
			      .shape = shape_id,
			      .normal = -manifold.normal,
			      .point = deepest.position,
			      .penetration = deepest.penetration,
			    }
			);
		}
	}
}

auto Simulator::overlapCapsule(
    BodyID ignored, const CapsuleShape& capsule, const glm::vec3& position, const glm::quat& rotation, float min_penetration,
    std::vector<QueryContact>& contacts
) const -> bool {
	ZoneScopedN("physics::OverlapCapsule");
	++m_profile.character_overlap_calls;

	contacts.clear();
	const Shape probe_shape = capsuleProbe(ignored, capsule);
	const Body probe_body = probeBody(position, rotation);
	const std::vector<ShapeID> candidates = queryCandidates(ignored, worldShapeBounds(probe_body, probe_shape));
	collideCapsuleProbe(probe_body, probe_shape, candidates, contacts);
	std::erase_if(contacts, [min_penetration](const QueryContact& contact) { return contact.penetration <= min_penetration; });
	return not contacts.empty();
}

auto Simulator::sweepCapsule(
    BodyID ignored, const CapsuleShape& capsule, const glm::quat& rotation, const glm::vec3& from, const glm::vec3& to, float skin
) const -> SweepHit {
	ZoneScopedN("physics::SweepCapsule");
	++m_profile.character_sweep_calls;

	SweepHit result {.position = to};
	const glm::vec3 delta = to - from;
	const float distance = glm::length(delta);
	if (not std::isfinite(distance) || distance <= 1.0e-6f) {
		result.position = from;
		return result;
	}
	const glm::vec3 direction = delta / distance;

	CapsuleShape inflated = capsule;
	inflated.radius += skin;
	inflated.height += 2.0f * skin;
	const Shape probe_shape = capsuleProbe(ignored, inflated);
	Body probe_body = probeBody(from, rotation);

	const AABB start_bounds = worldShapeBounds(probe_body, probe_shape);
	const AABB end_bounds = worldShapeBounds(probeBody(to, rotation), probe_shape);
	const std::vector<ShapeID> candidates = queryCandidates(ignored, combine(start_bounds, end_bounds));
	if (candidates.empty()) {
		return result;
	}

	// resting on or sliding off a surface must never block so only contacts the move digs into count
	std::vector<QueryContact> contacts;
	const auto blocking_contact = [&](float t) -> std::optional<QueryContact> {
		probe_body.position = from + delta * t;
		contacts.clear();
		collideCapsuleProbe(probe_body, probe_shape, candidates, contacts);

		std::optional<QueryContact> deepest;
		for (const QueryContact& contact : contacts) {
			if (contact.penetration <= skin * 0.5f || glm::dot(direction, contact.normal) >= -k_approach_epsilon) {
				continue;
			}
			if (not deepest.has_value() || contact.penetration > deepest->penetration) {
				deepest = contact;
			}
		}
		return deepest;
	};

	// samples closer than the radius so thin geometry cannot fall between two of them
	const float sample_step = std::max(capsule.radius * 0.5f, skin) / distance;
	float clear = 0.0f;
	float blocked = 1.0f;
	std::optional<QueryContact> hit;
	for (float t = std::min(sample_step, 1.0f);; t = std::min(t + sample_step, 1.0f)) {
		hit = blocking_contact(t);
		if (hit.has_value()) {
			blocked = t;
			break;
		}
		clear = t;
		if (t >= 1.0f) {
			return result;
		}
	}

	const auto refine_iterations = static_cast<int>(tunables().sweep_refine_iterations);
	for (int iteration = 0; iteration < refine_iterations; ++iteration) {
		const float middle = 0.5f * (clear + blocked);
		if (auto contact = blocking_contact(middle)) {
			blocked = middle;
			hit = contact;
		} else {
			clear = middle;
		}
	}

	return SweepHit {
	  .hit = true,
	  .fraction = clear,
	  .position = from + delta * clear,
	  .normal = hit->normal,
	  .point = hit->point,
	  .body = hit->body,
	  .shape = hit->shape,
	};
}

auto Simulator::setCapsuleShape(BodyID body, const CapsuleShape& capsule) -> bool {
	if (not mainThreadMutationAllowed()) {
		return false;
	}

	Shape* shape = nullptr;
	ShapeID shape_id;
	for (const NodeBinding& binding : m_node_bindings) {
		if (binding.body != body) {
			continue;
		}
		for (const ColliderBinding& collider : binding.colliders) {
			if (Shape* candidate = tryGetShape(collider.shape); candidate != nullptr && candidate->type == ShapeType::capsule) {
				shape = candidate;
				shape_id = collider.shape;
				break;
			}
		}
		break;
	}
	const float rotation_length_squared = glm::dot(capsule.local_rotation, capsule.local_rotation);
	const bool valid_capsule = std::isfinite(capsule.radius) && capsule.radius > 0.0f && std::isfinite(capsule.height) &&
	                           capsule.height >= 2.0f * capsule.radius && std::isfinite(rotation_length_squared) &&
	                           rotation_length_squared > 1.0e-10f;
	if (shape == nullptr || not valid_capsule) {
		return false;
	}

	shape->capsule = capsule;
	shape->capsule.local_rotation = glm::normalize(capsule.local_rotation);
	incrementShapeRevision(shape_id);
	wakeBodiesTouching(shape_id);
	return true;
}

void Simulator::moveKinematicBody(BodyID id, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& velocity) {
	if (not mainThreadMutationAllowed()) {
		return;
	}

	Body* body = tryGetBody(id);
	if (body == nullptr || body->type != BodyType::kinematic_body) {
		return;
	}

	const bool moved = body->position != position;
	body->position = position;
	body->rotation = glm::normalize(rotation);
	body->linear_velocity = velocity;
	body->angular_velocity = {};

	if (not moved) {
		return;
	}
	for (const NodeBinding& binding : m_node_bindings) {
		if (binding.body != id) {
			continue;
		}
		for (const ColliderBinding& collider : binding.colliders) {
			if (const Shape* shape = tryGetShape(collider.shape)) {
				wakeBodiesInBounds(worldShapeBounds(*body, *shape).expanded(k_kinematic_wake_margin));
			}
		}
		break;
	}
}

void Simulator::pushBody(BodyID id, const glm::vec3& point, const glm::vec3& direction, float speed, float max_impulse) {
	if (not mainThreadMutationAllowed()) {
		return;
	}

	Body* body = tryGetBody(id);
	if (body == nullptr || not body->enabled || body->type != BodyType::dynamic_body || body->inverse_mass <= 0.0f ||
	    max_impulse <= 0.0f) {
		return;
	}

	const float missing_speed = speed - glm::dot(body->linear_velocity, direction);
	if (missing_speed <= 0.0f) {
		return;
	}

	if (body->sleep_locked) {
		unlockSleep(id);
	}
	wakeBody(id);

	const glm::vec3 impulse = direction * std::min(missing_speed / body->inverse_mass, max_impulse);
	body->linear_velocity += impulse * body->inverse_mass;
	body->angular_velocity += body->inverse_inertia_world * glm::cross(point - body->worldCenterOfMass(), impulse);
}

}
