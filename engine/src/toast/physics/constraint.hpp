/**
 * @file constraint.hpp
 * @author Xein
 * @date 11 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include "collision.hpp"

#include <glm/vec3.hpp>

namespace physics {

struct Constraint {
	BroadPhasePair pair;
	uint8_t normal_index = k_primitive_manifold_normal_index;
	ContactFeatureID feature_a = {};
	ContactFeatureID feature_b = {};
	BodyID body_a;
	BodyID body_b;
	glm::vec3 contact_point = {};
	glm::vec3 normal = {};
	glm::vec3 tangent = {};
	glm::vec3 r_a = {};
	glm::vec3 r_b = {};
	float penetration = 0.0f;

	float normal_mass = 0.0f;
	float tangent_mass = 0.0f;
	float restitution_bias = 0.0f;

	float static_friction = 0.6f;
	float dynamic_friction = 0.4f;
	float accumulated_normal_impulse = 0.0f;
	float accumulated_tangent_impulse = 0.0f;
};

}
