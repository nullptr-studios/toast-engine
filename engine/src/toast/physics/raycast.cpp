#include "toast/physics/broad_phase.hpp"
#include "toast/physics/shape.hpp"
#include "toast/physics/simulator.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <optional>
#include <queue>
#include <toast/renderer/vulkan_renderer.hpp>
#include <tracy/Tracy.hpp>

namespace {
using namespace physics;

struct LocalHit {
	float distance;
	glm::vec3 normal;
};

/// @brief Raycast To Sphere
auto raycastSphere(const SphereShape& obj, glm::vec3 pos, glm::vec3 dir) -> std::optional<LocalHit> {
	ZoneScoped;
	// Solve the ray/sphere intersection in local space.
	float b = glm::dot(pos, dir);
	float c = dot(pos, pos) - (obj.radius * obj.radius);
	float h = (b * b) - c;

	// Ray Misses the sphere
	if (h < 0.0f) {
		return std::nullopt;
	}
	h = std::sqrt(h);
	// The Ray has 2 intersections entry and exit
	// This check is needed if ray starts from inside a sphere
	float d1 = -b - h;
	float t = (d1 >= 0.0f) ? d1 : -b + h;

	// Both Raycast intersection are behind the ray
	if (t < 0.0f) {
		return std::nullopt;
	}

	// calculate hit and normal
	glm::vec3 hit_pos = pos + dir * t;
	glm::vec3 normal = glm::normalize(hit_pos);

	return LocalHit {
	  .distance = t,
	  .normal = normal,
	};
}

/// @brief Raycast to AABB
auto raycastBox(const BoxShape& obj, glm::vec3 pos, glm::vec3 dir) -> std::optional<LocalHit> {
	ZoneScoped;
	// Calculate Half Extents
	glm::vec3 half_extents = obj.size * 0.5f;

	// values for the entry and exit intersection of the ray (dir * t)
	float entry = -std::numeric_limits<float>::infinity();
	float exit = std::numeric_limits<float>::infinity();
	glm::vec3 entry_normal {};
	glm::vec3 exit_normal {};

	// check intersection for each axis of the AABB
	for (int axis = 0; axis < 3; ++axis) {
		// If Raycast is Parallel
		if (std::abs(dir[axis]) <= 1.0e-6f) {
			// If Raycast is Outside the axis, It does not intersect
			if (pos[axis] < -half_extents[axis] || pos[axis] > half_extents[axis]) {
				return std::nullopt;
			}
			// Otherwise check next axis
			continue;
		}

		// Near and Far represent the entry and exit plane of that axis
		float near = (-half_extents[axis] - pos[axis]) / dir[axis];
		float far = (half_extents[axis] - pos[axis]) / dir[axis];
		glm::vec3 near_normal {};
		glm::vec3 far_normal {};
		near_normal[axis] = -1.0f;
		far_normal[axis] = 1.0f;

		// Make sure entry plane is the entry :P
		if (near > far) {
			std::swap(near, far);
			std::swap(near_normal, far_normal);
		}

		// Check if Plane entry/exit are more correct
		if (near > entry) {
			entry = near;
			entry_normal = near_normal;
		}
		if (far < exit) {
			exit = far;
			exit_normal = far_normal;
		}

		// entry plane should always be closest
		if (entry > exit) {
			return std::nullopt;
		}
	}

	// Ray Intersection is behind the ray
	if (exit < 0.0f) {
		return std::nullopt;
	}

	// The Ray has 2 intersections entry and exit
	// This check is needed if ray starts from inside a sphere
	return entry >= 0.0f ? LocalHit {.distance = entry, .normal = entry_normal}
	                     : LocalHit {.distance = exit, .normal = exit_normal};
}

/// @brief Raycast to Sphere
auto raycastCapsule(const CapsuleShape& obj, glm::vec3 pos, glm::vec3 dir) -> std::optional<LocalHit> {
	ZoneScoped;
	float shaft_half_length = (obj.height * 0.5f) - obj.radius;

	// A short capsule is a sphere
	if (shaft_half_length <= 1.0e-6f) {
		return raycastSphere(SphereShape {.radius = obj.radius}, pos, dir);
	}

	std::optional<LocalHit> closest;
	auto consider = [&closest](LocalHit candidate) {
		if (!closest || candidate.distance < closest->distance) {
			closest = candidate;
		}
	};

	// Shaft Intersection
	float shaft_a = (dir.x * dir.x) + (dir.y * dir.y);
	if (shaft_a > 1.0e-6f) {
		float shaft_b = (pos.x * dir.x) + (pos.y * dir.y);
		float shaft_c = (pos.x * pos.x) + (pos.y * pos.y) - (obj.radius * obj.radius);
		float discriminant = (shaft_b * shaft_b) - (shaft_a * shaft_c);
		if (discriminant >= 0.0f) {
			float root = std::sqrt(discriminant);
			for (float t : {(-shaft_b - root) / shaft_a, (-shaft_b + root) / shaft_a}) {
				float z = pos.z + (dir.z * t);
				if (t < 0.0f || z < -shaft_half_length || z > shaft_half_length) {
					continue;
				}
				glm::vec3 hit_pos = pos + dir * t;
				consider(LocalHit {.distance = t, .normal = glm::normalize(glm::vec3 {hit_pos.x, hit_pos.y, 0.0f})});
			}
		}
	}

	struct Cap {
		glm::vec3 center;
		float outward_z;
	};

	auto caps = {
	  Cap {{0.0f, 0.0f, -shaft_half_length}, -1.0f},
     Cap { {0.0f, 0.0f, shaft_half_length},  1.0f}
	};

	// Sphere Cap Intersection
	for (const Cap& cap : caps) {
		glm::vec3 cap_pos = pos - cap.center;
		float b = glm::dot(cap_pos, dir);
		float c = glm::dot(cap_pos, cap_pos) - (obj.radius * obj.radius);
		float discriminant = (b * b) - c;
		if (discriminant < 0.0f) {
			continue;
		}

		float root = std::sqrt(discriminant);
		for (float t : {-b - root, -b + root}) {
			if (t < 0.0f) {
				continue;
			}
			glm::vec3 hit_offset = cap_pos + dir * t;
			if (cap.outward_z * hit_offset.z < 0.0f) {
				continue;
			}
			consider(LocalHit {.distance = t, .normal = glm::normalize(hit_offset)});
		}
	}

	return closest;
}

/// @brief Raycast to Voxel
auto raycastVoxel(const VoxelShape& shape, glm::vec3 pos, glm::vec3 dir, float max_distance) -> std::optional<LocalHit> {
	ZoneScoped;
	const VoxelShapeData* shape_data = Simulator::tryGetVoxelDataConst(shape.data);
	if (shape_data == nullptr || shape_data->volume == nullptr) {
		return std::nullopt;
	}

	auto& volume = *shape_data->volume;
	const glm::ivec3 voxel_dims = glm::ivec3(volume.voxelDims());
	if (glm::any(glm::lessThanEqual(voxel_dims, glm::ivec3(0)))) {
		return std::nullopt;
	}

	// Volume Bounds Intersection
	const glm::vec3 volume_max = glm::vec3(voxel_dims) * voxel::k_voxel_size;
	float entry = -std::numeric_limits<float>::infinity();
	float exit = std::numeric_limits<float>::infinity();
	glm::vec3 entry_normal {};

	// AABB Intersection To get the entry and exit
	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(dir[axis]) <= 1.0e-6f) {
			if (pos[axis] < 0.0f || pos[axis] > volume_max[axis]) {
				return std::nullopt;
			}
			continue;
		}
		float near = -pos[axis] / dir[axis];
		float far = (volume_max[axis] - pos[axis]) / dir[axis];
		glm::vec3 near_normal {};
		near_normal[axis] = -1.0f;
		if (near > far) {
			std::swap(near, far);
			near_normal[axis] = 1.0f;
		}
		if (near > entry) {
			entry = near;
			entry_normal = near_normal;
		}
		exit = std::min(exit, far);
		if (entry > exit) {
			return std::nullopt;
		}
	}
	if (exit < 0.0f) {
		return std::nullopt;
	}
	if (max_distance >= 0.0f) {
		exit = std::min(exit, max_distance);
	}
	/// End of AABB Intersection

	// DDA Setup
	float t = std::max(entry, 0.0f);
	const glm::vec3 initial_point = pos + dir * t;
	glm::ivec3 voxel_coord = glm::ivec3(glm::floor(initial_point / voxel::k_voxel_size));

	// If starting exactly on an edge, select the voxel just inside the volume
	for (int axis = 0; axis < 3; ++axis) {
		if (voxel_coord[axis] == voxel_dims[axis] && dir[axis] < 0.0f) {
			voxel_coord[axis] = voxel_dims[axis] - 1;
		} else if (voxel_coord[axis] == -1 && dir[axis] > 0.0f) {
			voxel_coord[axis] = 0;
		}
	}

	auto outside_volume = [&voxel_dims](glm::ivec3 coord) {
		return glm::any(glm::lessThan(coord, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(coord, voxel_dims));
	};
	if (outside_volume(voxel_coord)) {
		return std::nullopt;
	}

	// Next Boundary Distance for each axis
	const glm::ivec3 step = glm::ivec3(glm::sign(dir));
	glm::vec3 t_delta {std::numeric_limits<float>::infinity()};
	glm::vec3 t_next {std::numeric_limits<float>::infinity()};
	for (int axis = 0; axis < 3; ++axis) {
		if (step[axis] == 0) {
			continue;
		}
		float boundary = (step[axis] > 0 ? static_cast<float>(voxel_coord[axis] + 1) : static_cast<float>(voxel_coord[axis])) *
		                 voxel::k_voxel_size;
		t_next[axis] = t + ((boundary - initial_point[axis]) / dir[axis]);
		t_delta[axis] = voxel::k_voxel_size / std::abs(dir[axis]);
	}

	// If starting inside the volume the normal faces the opposite ray direction
	glm::vec3 normal = entry >= 0.0f ? entry_normal : -dir;
	while (t <= exit) {
		// Return the first Solid Voxel hit
		if (volume.materialAt(voxel_coord) != voxel::k_empty_palette_index) {
			return LocalHit {.distance = t, .normal = glm::normalize(normal)};
		}

		// Move to next Voxel Boundary
		const float next_t = std::min({t_next.x, t_next.y, t_next.z});
		if (!std::isfinite(next_t) || next_t > exit) {
			break;
		}

		normal = {};
		for (int axis = 0; axis < 3; ++axis) {
			// Move across every axis that reaches this boundary at the same time
			if (t_next[axis] > next_t + 1.0e-6f) {
				continue;
			}
			voxel_coord[axis] += step[axis];
			t_next[axis] += t_delta[axis];
			normal[axis] = -static_cast<float>(step[axis]);
		}
		if (outside_volume(voxel_coord)) {
			break;
		}
		t = next_t;
	}
	return std::nullopt;
}

}

namespace physics {

auto raycast(glm::vec3 pos, glm::vec3 dir, float max_distance, int max_targets) -> std::vector<RayHit> {
	return Simulator::raycast(pos, dir, max_distance, max_targets);
}

auto Simulator::raycast(glm::vec3 pos, glm::vec3 dir, float max_distance, int max_targets) -> std::vector<RayHit> {
	ZoneScoped;
	if (instance == nullptr) {
		return {};
	}
	if (!std::isfinite(max_distance) || (max_distance < 0.0f && max_distance != -1.0f) || max_targets < -1 || max_targets == 0) {
		return {};
	}

	// normalize and get inverse of direction
	const float length = glm::length(dir);
	if (!(std::isfinite(length) && length > 1.0e-6f)) {
		return {};
	}
	dir /= length;
	glm::vec3 inv_dir = 1.0f / dir;

	// Gather possible ray collisions from the AABB tree.
	std::vector<ShapeID> candidates = instance->m_broad_phase.queryRay(pos, inv_dir, max_distance);
	if (candidates.empty()) {
		return {};
	}

	/// Iterate Through Potential Intersections ///
	std::vector<RayHit> results;
	if (max_targets <= 0) {
		results.reserve(candidates.size());
	}

	const bool is_target_limited = max_targets >= 0;
	auto nearer = [](const RayHit& lhs, const RayHit& rhs) { return lhs.distance < rhs.distance; };
	std::priority_queue<RayHit, std::vector<RayHit>, decltype(nearer)> nearest_hits {nearer};

	for (ShapeID target : candidates) {
		Shape* shape = instance->tryGetShape(target);
		if (shape == nullptr || not shape->enabled) {
			continue;
		}
		Body* body = instance->tryGetBody(shape->owner);
		if (body == nullptr) {
			continue;
		}

		// Skip if ray doesnt hit bounding box
		auto bounds_hit = worldShapeBounds(*body, *shape).intersectRay(pos, inv_dir);
		if (!bounds_hit || bounds_hit->t_max < 0.0f || (max_distance >= 0.0f && bounds_hit->t_min > max_distance) ||
		    (is_target_limited && nearest_hits.size() == static_cast<size_t>(max_targets) &&
		     bounds_hit->t_min > nearest_hits.top().distance)) {
			continue;
		}

		// Transform Ray to local space of the target
		glm::quat local_rotation = shape->getLocalRotation();
		glm::quat inv_body_rotation = glm::conjugate(body->rotation);
		glm::quat inv_shape_rotation = glm::conjugate(local_rotation);
		glm::quat combined_inv_rotation = inv_shape_rotation * inv_body_rotation;

		// Local Ray
		glm::vec3 local_pos = inv_shape_rotation * (inv_body_rotation * (pos - body->position) - shape->getLocalCenter());
		glm::vec3 local_dir = combined_inv_rotation * dir;

		// Calculate Raycast
		std::optional<LocalHit> hit;
		switch (shape->type) {
			case ShapeType::sphere: hit = raycastSphere(shape->sphere, local_pos, local_dir); break;
			case ShapeType::box: hit = raycastBox(shape->box, local_pos, local_dir); break;
			case ShapeType::capsule: hit = raycastCapsule(shape->capsule, local_pos, local_dir); break;
			case ShapeType::voxel: hit = raycastVoxel(shape->voxel, local_pos, local_dir, max_distance); break;
		}

		if (hit && (max_distance < 0.0f || hit->distance <= max_distance)) {
			// Bring the local hit back to world space.
			glm::quat combined_rotation = body->rotation * local_rotation;
			glm::vec3 world_normal = glm::normalize(combined_rotation * hit->normal);
			glm::vec3 hit_pos = pos + dir * hit->distance;
			RayHit ray_hit {
			  .node = colliderFor(shape->owner, target),
			  .position = hit_pos,
			  .normal = world_normal,
			  .distance = hit->distance,
			};

			if (is_target_limited) {
				if (nearest_hits.size() < static_cast<size_t>(max_targets)) {
					nearest_hits.push(std::move(ray_hit));
				} else if (ray_hit.distance < nearest_hits.top().distance) {
					nearest_hits.pop();
					nearest_hits.push(std::move(ray_hit));
				} else {
					continue;
				}
			} else {
				results.emplace_back(std::move(ray_hit));
			}

			debug::drawLine(pos, hit_pos, {0, 0, 1, 1});

			results.emplace_back(
			    RayHit {
			      .node = colliderFor(shape->owner, target),
			      .position = hit_pos,
			      .normal = world_normal,
			      .distance = hit->distance,
			      .body = shape->owner,
			    }
			);
		}
	}

	if (results.empty()) {
		debug::drawLine(pos, pos * dir * max_distance, {1, 0, 0, 1});
	}

	if (is_target_limited) {
		results.reserve(nearest_hits.size());
		while (not nearest_hits.empty()) {
			results.emplace_back(nearest_hits.top());
			nearest_hits.pop();
		}
	}

	// Sort all returned hits by distance. With a target limit, this sorts at most max_targets entries.
	std::ranges::sort(results, {}, &RayHit::distance);
	return results;
}
}
