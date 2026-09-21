/**
 * @file manifold.hpp
 * @author Xein
 * @date 10 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "collision.hpp"
#include "voxel_shape_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace physics {

struct ContactMaterial {
	float restitution = 0.0f;
	float static_friction = 0.6f;
	float dynamic_friction = 0.4f;
};

struct ContactPoint {
	glm::vec3 position = {};
	float penetration = 0.0f;
	ContactFeatureID feature_a = {};
	ContactFeatureID feature_b = {};
	ContactMaterial material;
};

struct Manifold {
	BroadPhasePair pair;
	glm::vec3 normal = {};
	uint8_t normal_index = k_primitive_manifold_normal_index;
	std::array<ContactPoint, 4> contacts = {};
	uint8_t contact_count = 0;
};

[[nodiscard]]
inline bool operator<(const Manifold& lhs, const Manifold& rhs) {
	if (lhs.pair != rhs.pair) {
		return lhs.pair < rhs.pair;
	}

	if (lhs.normal_index != rhs.normal_index) {
		return lhs.normal_index < rhs.normal_index;
	}

	if (lhs.contact_count != rhs.contact_count) {
		return lhs.contact_count < rhs.contact_count;
	}

	for (std::size_t index = 0; index < lhs.contact_count && index < lhs.contacts.size(); ++index) {
		const ContactPoint& lhs_contact = lhs.contacts[index];
		const ContactPoint& rhs_contact = rhs.contacts[index];

		if (lhs_contact.feature_a != rhs_contact.feature_a) {
			return lhs_contact.feature_a < rhs_contact.feature_a;
		}
		if (lhs_contact.feature_b != rhs_contact.feature_b) {
			return lhs_contact.feature_b < rhs_contact.feature_b;
		}
	}

	return false;
}

struct CachedContact {
	ContactFeatureID feature_a = {};
	ContactFeatureID feature_b = {};
	float normal_impulse = 0.0f;
	glm::vec3 tangent_impulse = {};
};

struct CachedManifold {
	BroadPhasePair pair;
	uint8_t normal_index = k_primitive_manifold_normal_index;
	uint32_t shape_a_revision = 0;
	uint32_t shape_b_revision = 0;
	std::array<CachedContact, 4> contacts = {};
	uint8_t contact_count = 0;
};

namespace _detail {

inline constexpr float direction_epsilon_sq = 1.0e-10f;
inline constexpr float parallel_axis_epsilon_sq = 1.0e-10f;
inline constexpr float contact_tolerance = 1.0e-5f;
inline constexpr float duplicate_point_epsilon_sq = 1.0e-8f;
inline constexpr float unit_normal_tolerance = 1.0e-3f;

struct WorldSphere {
	glm::vec3 center;
	float radius;
};

struct WorldCapsule {
	glm::vec3 point_a;
	glm::vec3 point_b;
	float radius;
};

struct WorldBox {
	glm::vec3 center;
	glm::mat3 rotation;
	glm::vec3 half_extents;
};

struct SegmentClosestPoint {
	glm::vec3 point;
	float parameter;
};

struct SegmentClosestPoints {
	glm::vec3 point_a;
	glm::vec3 point_b;
	float parameter_a;
	float parameter_b;
};

enum class BoxFeatureType : uint8_t {
	interior,
	face,
	edge,
	vertex
};

struct BoxFeature {
	BoxFeatureType type = BoxFeatureType::interior;
	uint8_t axis_mask = 0;
	uint8_t positive_mask = 0;
	auto operator<=>(const BoxFeature&) const = default;
};

struct SegmentBoxClosestPoints {
	glm::vec3 segment_point;
	glm::vec3 box_point;
	float segment_parameter;
	BoxFeature box_feature;
};

[[nodiscard]]
auto boxFeature(const glm::vec3& point, const glm::vec3& half_extents) -> BoxFeature;
[[nodiscard]]
auto contactFeature(const BoxFeature& feature) -> ContactFeatureID;
[[nodiscard]]
auto worldSphere(const Body& body, const SphereShape& sphere) -> WorldSphere;
[[nodiscard]]
auto worldCapsule(const Body& body, const CapsuleShape& capsule) -> WorldCapsule;
[[nodiscard]]
auto worldBox(const Body& body, const BoxShape& box) -> WorldBox;
[[nodiscard]]
auto closestPointOnSegment(const glm::vec3& point, const glm::vec3& segment_a, const glm::vec3& segment_b) -> SegmentClosestPoint;
[[nodiscard]]
auto closestPointsBetweenSegments(const glm::vec3& a0, const glm::vec3& a1, const glm::vec3& b0, const glm::vec3& b1)
    -> SegmentClosestPoints;
[[nodiscard]]
auto closestPointOnBox(const glm::vec3& point, const WorldBox& box) -> glm::vec3;
[[nodiscard]]
auto closestPointsSegmentBox(const glm::vec3& segment_a, const glm::vec3& segment_b, const WorldBox& box)
    -> SegmentBoxClosestPoints;
[[nodiscard]]
auto perpendicularTo(const glm::vec3& segment) -> glm::vec3;
[[nodiscard]]
auto combineMaterials(const PhysicsMaterial& a, const PhysicsMaterial& b) -> ContactMaterial;

}

struct CollisionElement {
	const Shape& shape;
	const Body& body;
};

[[nodiscard]]
auto collideSpheres(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold>;

[[nodiscard]]
auto collideSphereBox(BroadPhasePair pair, CollisionElement sphere, CollisionElement box) -> std::optional<Manifold>;

[[nodiscard]]
auto collideSphereCapsule(BroadPhasePair pair, CollisionElement sphere, CollisionElement capsule) -> std::optional<Manifold>;

void collideSphereVoxel(
    BroadPhasePair pair, CollisionElement sphere, CollisionElement voxel, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
);

[[nodiscard]]
auto collideBoxes(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold>;

void collideBoxVoxel(
    BroadPhasePair pair, CollisionElement box, CollisionElement voxel, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
);

[[nodiscard]]
auto collideCapsuleBox(BroadPhasePair pair, CollisionElement capsule, CollisionElement box) -> std::optional<Manifold>;

void collideCapsuleVoxel(
    BroadPhasePair pair, CollisionElement capsule, CollisionElement voxel, const VoxelShapeData& voxel_data,
    std::vector<Manifold>& output
);

[[nodiscard]]
auto collideCapsules(BroadPhasePair pair, CollisionElement a, CollisionElement b) -> std::optional<Manifold>;

}
