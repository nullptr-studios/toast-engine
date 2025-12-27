/// @file resolutions.h
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include <Engine/Physics/Line.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <algorithm>
#include <algorithm>

void _rb_line_collision(physics::RigidbodyData& rb, physics::Line& l) {
	// calculate normal of the segment
	double distance_along_line = glm::dot(rb.position - l.point, l.tangent);
	// Clamp to segment and get closest point
	double t = glm::clamp(distance_along_line, 0.0, l.length);
	glm::dvec2 closest = l.point + t * l.tangent;

	// Contact normal from closest point
	glm::dvec2 contact_vec = rb.position - closest;
	double dist = glm::length(contact_vec);
	if (dist <= 0.0) return;
	glm::dvec2 normal = contact_vec / dist;

	// Rebuild tangent
	glm::dvec2 tangent = glm::normalize(glm::dvec2{ -normal.y, normal.x });

	// Get the penetration
	double penetration = rb.radius - dist;

	// if the penetration is 0 or less it means theres no intersection with the line
	// TODO: we will maybe need substeping for this if the speed is really high
	if (penetration <= 0) return;

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

void _rb_rb_collision(physics::RigidbodyData& rb_1, physics::RigidbodyData& rb_2) {
		glm::dvec2 delta = rb_2.position - rb_1.position;
		double dist = glm::length(delta);
		if (dist < 1e-12) return; // avoid NaN
		glm::dvec2 normal = delta / dist;
		glm::dvec2 tangent{ normal.y, -normal.x };

		double penetration = (rb_1.radius + rb_2.radius) - dist;

		// Relative velocity
		glm::dvec2 rv = rb_1.velocity - rb_2.velocity;
		double rel_norm = glm::dot(rv, normal);
		double rel_tan  = glm::dot(rv, tangent);

		// Replace with your masses; keep as 1.0 if you assume unit mass
		const double inv_mass1 = 1.0;
		const double inv_mass2 = 1.0;
		const double inv_mass_sum = inv_mass1 + inv_mass2;

		constexpr double RESTITUTION = 0.6;  // bounciness
		constexpr double FRICTION    = 0.05; // Coulomb coefficient

		// Normal impulse only if closing
		double jn = 0.0;
		if (rel_norm < 0.0) {
				jn = -(1.0 + RESTITUTION) * rel_norm / inv_mass_sum;
		}

		// Tangential (friction) impulse, clamped by Coulomb
		double jt = -rel_tan / inv_mass_sum;
		double max_friction = FRICTION * jn;
		jt = std::min(jt, max_friction);
		jt = std::max(jt, -max_friction);

		glm::dvec2 impulse = jn * normal + jt * tangent;

		// Apply impulses (note signs)
		rb_1.velocity += impulse * inv_mass1;
		rb_2.velocity -= impulse * inv_mass2;

		// Debug draw
		renderer::DebugLine(rb_1.position, rb_1.position + impulse, {1.0f, 0.0f, 1.0f, 1.0f});
		renderer::DebugLine(rb_2.position, rb_2.position - impulse, {1.0f, 0.0f, 1.0f, 1.0f});

		// Positional correction (Baumgarte-style) to prevent sinking
		if (penetration > 0.0) {
				constexpr double PERCENT = 0.8;     // push-out strength
				constexpr double SLOP    = 1e-4;    // allow tiny overlap
				double correction_mag = PERCENT * (penetration - SLOP) / inv_mass_sum;
				correction_mag = std::max(correction_mag, 0.0);
				glm::dvec2 correction = correction_mag * normal;
				rb_1.position -= correction * inv_mass1;
				rb_2.position += correction * inv_mass2;
		}
}
