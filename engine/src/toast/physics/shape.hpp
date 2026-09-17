/**
 * @file shape.hpp
 * @author Xein
 * @date 10 Sep 2026
 * @brief Shape identifiers, geometry, and physics-owned runtime records
 */

#pragma once

#include "aabb.hpp"
#include "body.hpp"

#include <compare>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>

namespace physics {

struct PhysicsMaterial {
	float restitution = 0.0f;
	float static_friction = 0.6f;
	float dynamic_friction = 0.4f;
};

struct ShapeID {
	uint32_t slot = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;
	auto operator<=>(const ShapeID&) const = default;
};

enum class FeatureType : uint8_t {
	sphere_surface = 1,
	capsule_cap_a,
	capsule_side,
	capsule_cap_b,
	box_face,
	box_edge,
	box_vertex,
	box_clip
};

struct ContactFeatureID {
	uint64_t value = 0;
	auto operator<=>(const ContactFeatureID&) const = default;
};

inline constexpr uint64_t contact_feature_payload_mask = 0x00ff'ffff'ffff'ffffULL;

[[nodiscard]]
constexpr auto makeContactFeature(FeatureType type, uint64_t payload = 0) -> ContactFeatureID {
	return ContactFeatureID {(static_cast<uint64_t>(type) << 56) | (payload & contact_feature_payload_mask)};
}

[[nodiscard]]
constexpr auto capsuleFeature(float parameter) -> ContactFeatureID {
	if (parameter <= 0.0f) {
		return makeContactFeature(FeatureType::capsule_cap_a);
	}
	if (parameter >= 1.0f) {
		return makeContactFeature(FeatureType::capsule_cap_b);
	}
	return makeContactFeature(FeatureType::capsule_side);
}

[[nodiscard]]
constexpr auto boxFaceFeature(int axis, bool positive) -> ContactFeatureID {
	return makeContactFeature(FeatureType::box_face, static_cast<uint64_t>(axis) | (static_cast<uint64_t>(positive) << 2));
}

[[nodiscard]]
constexpr auto boxEdgeFeature(int direction_axis, uint8_t positive_mask) -> ContactFeatureID {
	return makeContactFeature(
	    FeatureType::box_edge, static_cast<uint64_t>(direction_axis) | (static_cast<uint64_t>(positive_mask & 0x7u) << 2)
	);
}

[[nodiscard]]
constexpr auto boxVertexFeature(uint8_t positive_mask) -> ContactFeatureID {
	return makeContactFeature(FeatureType::box_vertex, positive_mask & 0x7u);
}

[[nodiscard]]
constexpr auto boxClipFeature(int reference_axis, bool reference_positive, int side_axis, bool side_positive)
    -> ContactFeatureID {
	uint64_t payload = static_cast<uint64_t>(reference_axis);
	payload |= static_cast<uint64_t>(reference_positive) << 2;
	payload |= static_cast<uint64_t>(side_axis) << 3;
	payload |= static_cast<uint64_t>(side_positive) << 5;
	return makeContactFeature(FeatureType::box_clip, payload);
}

enum class ShapeType : uint8_t {
	sphere,
	box,
	capsule,
	voxel
};

struct SphereShape {
	glm::vec3 local_center = {};
	float radius = 0.5f;
};

struct BoxShape {
	glm::vec3 local_center = {};
	glm::quat local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 size = {1.0f, 1.0f, 1.0f};
};

struct CapsuleShape {
	glm::vec3 local_center = {};
	glm::quat local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	float radius = 0.5f;
	float height = 1.0f;
};

struct VoxelDataID {
	uint32_t slot = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;
	auto operator<=>(const VoxelDataID&) const = default;
};

struct VoxelShape {
	VoxelDataID data;
	glm::vec3 local_center = {};
	glm::quat local_rotation = {1.0f, 0.0f, 0.0f, 0.0f};
	AABB local_bounds = {};
};

struct Shape {
	BodyID owner;
	ShapeType type = ShapeType::sphere;
	bool enabled = true;
	PhysicsMaterial material;

	union {
		SphereShape sphere;
		BoxShape box;
		CapsuleShape capsule;
		VoxelShape voxel;
	};
};

struct ShapeSlot {
	Shape shape;
	uint32_t generation = 1;
	uint32_t revision = 1;
	bool occupied = false;
};

inline constexpr ContactFeatureID sphere_surface_feature = makeContactFeature(FeatureType::sphere_surface);

}
