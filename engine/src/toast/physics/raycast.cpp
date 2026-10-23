#include "nodes/rigidbody.hpp"
#include "toast/physics/broad_phase.hpp"
#include "toast/physics/shape.hpp"
#include "toast/physics/simulator.hpp"
#include "toast/physics/voxel_query.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <optional>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

namespace {
using namespace physics;

constexpr float k_no_end = std::numeric_limits<float>::infinity();

struct LocalHit {
	float distance;
	glm::vec3 normal;
	bool inside = false;    ///< if true, normal is the one against the ray
};

[[nodiscard]]
auto startsInside(glm::vec3 direction) -> LocalHit {
	return LocalHit {.distance = 0.0f, .normal = -direction, .inside = true};
}

auto raycastSphere(float radius, glm::vec3 pos, glm::vec3 dir, float clip) -> std::optional<LocalHit> {
	// Solve the ray-sphere intersection in local space
	const float c = glm::dot(pos, pos) - (radius * radius);
	if (c < 0.0f) {
		return startsInside(dir);
	}

	// With c not negative both roots have the same sign
	// b is not negative when the sphere is behind the ray
	const float b = glm::dot(pos, dir);
	if (b >= 0.0f) {
		return std::nullopt;
	}
	const float h = (b * b) - c;
	if (h < 0.0f) {
		return std::nullopt;
	}

	const float t = -b - std::sqrt(h);
	if (t > clip) {
		return std::nullopt;
	}
	return LocalHit {.distance = t, .normal = glm::normalize(pos + (dir * t))};
}

auto raycastBox(glm::vec3 half_extents, glm::vec3 pos, glm::vec3 dir, float clip) -> std::optional<LocalHit> {
	// values for the entry and exit intersection of the ray (dir * t)
	float entry = -k_no_end;
	float exit = k_no_end;
	glm::vec3 entry_normal {};

	// check intersection for each axis of the box
	for (int axis = 0; axis < 3; ++axis) {
		// If Raycast is Parallel
		if (std::abs(dir[axis]) <= 1.0e-6f) {
			// If Raycast is Outside the axis, It does not intersect
			if (std::abs(pos[axis]) > half_extents[axis]) {
				return std::nullopt;
			}
			// Otherwise check next axis
			continue;
		}

		// The entry and exit plane of that axis
		const float inverse = 1.0f / dir[axis];
		const float t0 = (-half_extents[axis] - pos[axis]) * inverse;
		const float t1 = (half_extents[axis] - pos[axis]) * inverse;
		const float t_enter = std::min(t0, t1);
		const float t_leave = std::max(t0, t1);

		if (t_enter > entry) {
			entry = t_enter;
			entry_normal = glm::vec3 {};
			entry_normal[axis] = dir[axis] > 0.0f ? -1.0f : 1.0f;
		}
		exit = std::min(exit, t_leave);

		// entry plane should always be closest
		if (entry > exit) {
			return std::nullopt;
		}
	}

	// The box is behind the ray
	if (exit <= 0.0f) {
		return std::nullopt;
	}
	if (entry < 0.0f) {
		return startsInside(dir);
	}
	if (entry > clip) {
		return std::nullopt;
	}
	return LocalHit {.distance = entry, .normal = entry_normal};
}

auto raycastCapsule(float radius, float height, glm::vec3 pos, glm::vec3 dir, float clip) -> std::optional<LocalHit> {
	const float shaft_half_length = (height * 0.5f) - radius;

	// A short capsule is a sphere
	if (shaft_half_length <= 1.0e-6f) {
		return raycastSphere(radius, pos, dir, clip);
	}

	const glm::vec3 from_axis {pos.x, pos.y, pos.z - std::clamp(pos.z, -shaft_half_length, shaft_half_length)};
	if (glm::dot(from_axis, from_axis) < radius * radius) {
		return startsInside(dir);
	}

	std::optional<LocalHit> closest;
	float limit = clip;
	const auto consider = [&](float t, glm::vec3 normal) {
		if (t >= 0.0f && t <= limit) {
			limit = t;
			closest = LocalHit {.distance = t, .normal = normal};
		}
	};

	// Shaft Intersection
	const float shaft_a = (dir.x * dir.x) + (dir.y * dir.y);
	if (shaft_a > 1.0e-6f) {
		const float shaft_b = (pos.x * dir.x) + (pos.y * dir.y);
		const float shaft_c = (pos.x * pos.x) + (pos.y * pos.y) - (radius * radius);
		const float discriminant = (shaft_b * shaft_b) - (shaft_a * shaft_c);
		if (discriminant >= 0.0f) {
			const float root = std::sqrt(discriminant);
			for (const float t : {(-shaft_b - root) / shaft_a, (-shaft_b + root) / shaft_a}) {
				if (std::abs(pos.z + (dir.z * t)) <= shaft_half_length) {
					consider(t, glm::normalize(glm::vec3 {pos.x + (dir.x * t), pos.y + (dir.y * t), 0.0f}));
				}
			}
		}
	}

	// Sphere Cap Intersection
	for (const float side : {-1.0f, 1.0f}) {
		const glm::vec3 cap_pos = pos - glm::vec3 {0.0f, 0.0f, side * shaft_half_length};
		const float b = glm::dot(cap_pos, dir);
		const float c = glm::dot(cap_pos, cap_pos) - (radius * radius);
		const float discriminant = (b * b) - c;
		if (discriminant < 0.0f) {
			continue;
		}

		const float root = std::sqrt(discriminant);
		for (const float t : {-b - root, -b + root}) {
			const glm::vec3 hit_offset = cap_pos + (dir * t);
			if (side * hit_offset.z >= 0.0f) {
				consider(t, glm::normalize(hit_offset));
			}
		}
	}

	return closest;
}

}

namespace physics {

template<typename OnContact>
auto Simulator::castRay(const RayQuery& query, OnContact&& on_contact) const -> uint32_t {
	const glm::vec3 inverse_direction = 1.0f / query.direction;
	float clip = query.max_distance;
	uint32_t tested = 0;

	m_broad_phase.raycast(query.origin, inverse_direction, clip, [&](ShapeID shape_id, float) -> float {
		const Shape* shape = tryGetShape(shape_id);
		if (shape == nullptr || not shape->enabled || shape->owner == query.ignored) {
			return clip;
		}
		if (query.voxels_only && shape->type != ShapeType::voxel) {
			return clip;
		}
		const Body* body = tryGetBody(shape->owner);
		if (body == nullptr || not body->enabled || (body->layers.included & query.layers) == 0) {
			return clip;
		}
		++tested;

		// The ray in the space of the shape
		const glm::vec3 center = body->position + (body->rotation * shape->getLocalCenter());
		const glm::quat rotation = glm::normalize(body->rotation * shape->getLocalRotation());
		const glm::quat inverse_rotation = glm::conjugate(rotation);
		const glm::vec3 local_origin = inverse_rotation * (query.origin - center);
		const glm::vec3 local_direction = inverse_rotation * query.direction;

		const auto report = [&](float distance, glm::vec3 local_normal, bool inside, uint8_t palette_index) {
			const glm::vec3 normal = inside ? -query.direction : glm::normalize(rotation * local_normal);
			const RayContact contact {
			  .shape = shape_id,
			  .body = shape->owner,
			  .distance = distance,
			  .normal = normal,
			  .palette_index = palette_index,
			};
			clip = std::min(clip, on_contact(contact));
		};
		const auto report_local = [&](const std::optional<LocalHit>& hit) {
			if (hit) {
				report(hit->distance, hit->normal, hit->inside, 0);
			}
		};

		switch (shape->type) {
			case ShapeType::sphere: report_local(raycastSphere(shape->sphere.radius, local_origin, local_direction, clip)); break;
			case ShapeType::box: report_local(raycastBox(shape->box.size * 0.5f, local_origin, local_direction, clip)); break;
			case ShapeType::capsule:
				report_local(raycastCapsule(shape->capsule.radius, shape->capsule.height, local_origin, local_direction, clip));
				break;
			case ShapeType::voxel: {
				const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data);
				if (data == nullptr || data->volume == nullptr) {
					break;
				}
				const VoxelQueryContext context {*data->volume, data->surface, data->palette, data->materials};
				queryVoxelRay(
				    context,
				    shape->voxel.local_bounds,
				    local_origin,
				    local_direction,
				    clip,
				    query.hit_passable,
				    [&](const VoxelRayEntry& entry) {
					    report(entry.distance, entry.normal, false, entry.palette_index);
					    return clip;
				    }
				);
				break;
			}
		}
		return clip;
	});

	return tested;
}

auto Simulator::windBlocked(const glm::vec3& position, const glm::vec3& toward, float distance, BodyID ignored) const -> bool {
	const std::optional<RayQuery> query = makeRayQuery(position, toward, distance, ignored, k_all_layers);
	if (not query) {
		return false;
	}

	bool blocked = false;
	castRay(*query, [&](const RayContact& contact) {
		const Body* body = tryGetBody(contact.body);
		if (body == nullptr || body->type == BodyType::dynamic_body) {
			// Loose bodies don't shelter anything
			return query->max_distance;
		}
		blocked = true;
		return 0.0f;    // Nothing close
	});
	return blocked;
}

auto Simulator::makeRayQuery(glm::vec3 origin, glm::vec3 direction, float max_distance, BodyID ignored, uint32_t layers)
    -> std::optional<RayQuery> {
	const bool finite_origin = std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
	const float length = glm::length(direction);
	if (not finite_origin || std::isnan(max_distance) || not(std::isfinite(length) && length > 1.0e-6f)) {
		return std::nullopt;
	}

	return RayQuery {
	  .origin = origin,
	  .direction = direction / length,
	  .max_distance = max_distance < 0.0f ? k_no_end : max_distance,
	  .ignored = ignored,
	  .layers = layers,
	};
}

auto Simulator::makeRayHit(const RayQuery& query, const RayContact& contact, toast::Box<toast::Node> node) -> RayHit {
	return RayHit {
	  .node = std::move(node),
	  .position = query.origin + (query.direction * contact.distance),
	  .normal = contact.normal,
	  .distance = contact.distance,
	  .body = contact.body,
	  .shape = contact.shape,
	};
}

auto raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, BodyID ignored, const std::vector<int>& layers)
    -> std::optional<RayHit> {
	return Simulator::raycast(origin, direction, max_distance, ignored, layerMask(layers));
}

auto raymulticast(
    glm::vec3 origin, glm::vec3 direction, float max_distance, int max_hits, BodyID ignored, const std::vector<int>& layers
) -> std::vector<RayHit> {
	return Simulator::raymulticast(origin, direction, max_distance, max_hits, ignored, layerMask(layers));
}

auto linecast(glm::vec3 from, glm::vec3 to, int max_hits, BodyID ignored, const std::vector<int>& layers) -> std::vector<RayHit> {
	return Simulator::linecast(from, to, max_hits, ignored, layerMask(layers));
}

auto Simulator::raycast(glm::vec3 origin, glm::vec3 direction, float max_distance, BodyID ignored, uint32_t layers)
    -> std::optional<RayHit> {
	ZoneScopedN("physics::Raycast");
	const std::optional<RayQuery> query = makeRayQuery(origin, direction, max_distance, ignored, layers);
	if (instance == nullptr || not query) {
		return std::nullopt;
	}

	// Only nearest contact matters
	std::optional<RayContact> nearest;
	const uint32_t tested = instance->castRay(*query, [&](const RayContact& contact) {
		if (not nearest || contact.distance < nearest->distance) {
			nearest = contact;
		}
		return nearest->distance;
	});
	ZoneValue(static_cast<uint64_t>(tested));

	if (not nearest) {
		return std::nullopt;
	}
	return makeRayHit(*query, *nearest, colliderFor(nearest->body, nearest->shape));
}

auto Simulator::linecast(glm::vec3 from, glm::vec3 to, int max_hits, BodyID ignored, uint32_t layers) -> std::vector<RayHit> {
	ZoneScopedN("physics::Linecast");
	// A segment is a ray that ends at the second point, a degenerate or non finite one is not a ray and finds nothing
	const glm::vec3 delta = to - from;
	return raymulticast(from, delta, glm::length(delta), max_hits, ignored, layers);
}

auto Simulator::raymulticast(
    glm::vec3 origin, glm::vec3 direction, float max_distance, int max_hits, BodyID ignored, uint32_t layers
) -> std::vector<RayHit> {
	ZoneScopedN("physics::RayMulticast");
	const std::optional<RayQuery> query = makeRayQuery(origin, direction, max_distance, ignored, layers);
	if (instance == nullptr || not query || max_hits == 0) {
		return {};
	}

	const auto by_distance = [](const RayContact& lhs, const RayContact& rhs) { return lhs.distance < rhs.distance; };
	const bool limited = max_hits > 0;
	const auto limit = static_cast<size_t>(max_hits);
	std::vector<RayContact> contacts;
	if (limited) {
		contacts.reserve(limit + 1);
	}

	const uint32_t tested = instance->castRay(*query, [&](const RayContact& contact) {
		contacts.push_back(contact);
		if (not limited) {
			return query->max_distance;
		}

		std::ranges::push_heap(contacts, by_distance);
		if (contacts.size() > limit) {
			std::ranges::pop_heap(contacts, by_distance);
			contacts.pop_back();
		}
		return contacts.size() == limit ? contacts.front().distance : query->max_distance;
	});
	ZoneValue(static_cast<uint64_t>(tested));

	std::ranges::sort(contacts, {}, &RayContact::distance);

	std::vector<std::pair<ShapeID, toast::Box<toast::Node>>> nodes;
	std::vector<RayHit> hits;
	hits.reserve(contacts.size());
	for (const RayContact& contact : contacts) {
		auto known = std::ranges::find(nodes, contact.shape, &std::pair<ShapeID, toast::Box<toast::Node>>::first);
		if (known == nodes.end()) {
			nodes.emplace_back(contact.shape, colliderFor(contact.body, contact.shape));
			known = nodes.end() - 1;
		}
		hits.push_back(makeRayHit(*query, contact, known->second));
	}
	return hits;
}

auto Simulator::bodyFor(const toast::Node& node) -> BodyID {
	for (toast::Box<toast::Node> current = node.box(); current.exists(); current = current->parent()) {
		if (const toast::Box<Rigidbody> rigidbody = current.as<Rigidbody>(); rigidbody.exists()) {
			return rigidbody->bodyID();
		}
		if (const toast::Box<toast::VoxelNode> voxel = current.as<toast::VoxelNode>(); voxel.exists()) {
			for (const toast::VoxelPart& part : voxel->parts()) {
				if (part.body != BodyID {}) {
					return part.body;
				}
			}
			return voxel->bodyID();
		}
	}
	return {};
}

auto Simulator::marchVoxelRay(const glm::vec3& origin, const glm::vec3& dir, float max_distance) -> std::optional<VoxelRayHit> {
	const RayQuery query {
	  .origin = origin, .direction = dir, .max_distance = max_distance, .voxels_only = true, .hit_passable = true
	};

	std::optional<RayContact> nearest;
	castRay(query, [&](const RayContact& contact) {
		if (not nearest || contact.distance < nearest->distance) {
			nearest = contact;
		}
		return nearest->distance;
	});
	if (not nearest) {
		return std::nullopt;
	}

	const Shape* shape = tryGetShape(nearest->shape);
	const VoxelShapeData* data = shape != nullptr ? tryGetVoxelDataConst(shape->voxel.data) : nullptr;
	if (data == nullptr) {
		return std::nullopt;
	}

	return VoxelRayHit {
	  .shape = nearest->shape,
	  .distance = nearest->distance,
	};
}

}
