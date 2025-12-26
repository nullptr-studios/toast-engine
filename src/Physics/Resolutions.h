/// @file resolutions.h
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include <Engine/Physics/Line.hpp>
#include <Engine/Physics/Rigidbody.hpp>

void _rb_line_collision(physics::RigidbodyData& rb, physics::Line& l) {
	// Get the penetration
	double distance = glm::dot(rb.position - l.point, l.normal);
	double penetration = rb.radius - distance;

	// calculate normal of the segment
	double distance_along_line = glm::dot(rb.position - l.point, l.tangent);
	glm::dvec2 normal = l.normal;
	glm::dvec2 tangent = l.tangent;
	if (distance_along_line < 0) {
		normal = rb.position - l.point;
		tangent = { -normal.y, normal.x };
	} else if (distance_along_line > l.length) {
		// Be careful with l.tangent and l.normal to be normalized
		glm::dvec2 end_point = l.point + (l.tangent * l.length);
		normal = rb.position - end_point;
		tangent = { -normal.y, normal.x };
	}

	if (penetration > 0) {
		// Collision detected
		double normal_velocity = glm::dot(rb.velocity, normal);
		double tangent_velocity = glm::dot(rb.velocity, tangent);

		// TODO: This shoudn't be hardcoded
		constexpr float RESTITUTION = 0.8f;
		constexpr float FRICTION = 0.05f;

		// Velocity resolution
		if (normal_velocity < 0) {
			rb.velocity = (-normal_velocity * RESTITUTION * normal) + (tangent_velocity * (1 - FRICTION) * tangent);
		}

		// Positional resolution
		rb.position += normal * penetration;
	}
}
