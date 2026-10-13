/**
 * @file constraint.hpp
 * @author Xein
 * @date 11 Sep 2026
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include "collision.hpp"
#include "material_defaults.hpp"

#include <glm/vec2.hpp>
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
	glm::vec3 tangent_1 = {};
	glm::vec3 tangent_2 = {};
	glm::vec3 r_a = {};
	glm::vec3 r_b = {};
	float penetration = 0.0f;

	float normal_mass = 0.0f;
	float tangent_mass_1 = 0.0f;
	float tangent_mass_2 = 0.0f;
	float restitution_bias = 0.0f;

	float static_friction = material_defaults::k_static_friction;
	float dynamic_friction = material_defaults::k_dynamic_friction;
	float accumulated_normal_impulse = 0.0f;
	glm::vec2 accumulated_tangent_impulse = {};
	float rolling_friction = 0.0f;
	float spin_friction = 0.0f;
	float rolling_mass_1 = 0.0f;
	float rolling_mass_2 = 0.0f;
	float spin_mass = 0.0f;
	glm::vec2 accumulated_rolling_impulse = {};
	float accumulated_spin_impulse = 0.0f;
};

}
