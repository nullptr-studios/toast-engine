/// @file Resolutions.h
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include <Engine/Physics/Line.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <algorithm>

namespace {
	constexpr double RESTITUTION       = 0.6;
	constexpr double REST_THRESHOLD    = 0.5;
	constexpr double FRICTION_COEFF    = 0.05;
	constexpr double POS_CORRECT_PCT   = 0.3;
	constexpr double POS_CORRECT_SLOP  = 1e-3;
	constexpr double INV_MASS          = 1.0;
}

void _rb_line_collision(physics::RigidbodyData& rb, const physics::Line& l) {
	// Ensure tangent is normalized
	glm::dvec2 tangent_line = glm::normalize(l.tangent);
	double t = glm::clamp(glm::dot(rb.position - l.point, tangent_line), 0.0, l.length);
	glm::dvec2 closest = l.point + t * tangent_line;

	glm::dvec2 contact_vec = rb.position - closest;
	double dist = glm::length(contact_vec);
	if (dist <= 1e-12) return;

	glm::dvec2 normal = contact_vec / dist;
	glm::dvec2 tangent{ -normal.y, normal.x };

	double penetration = rb.radius - dist;
	if (penetration <= 0.0) return;

	double rel_norm = glm::dot(rb.velocity, normal);
	double rel_tan  = glm::dot(rb.velocity, tangent);

	double e = (std::abs(rel_norm) < REST_THRESHOLD) ? 0.0 : RESTITUTION;

	// line is static, so only rb changes
	if (rel_norm < 0.0) {
		double jn = -(1.0 + e) * rel_norm; // inv_mass_line = 0
		double jt = -rel_tan;
		double max_friction = FRICTION_COEFF * jn;
		jt = glm::clamp(jt, -max_friction, max_friction);

		rb.velocity += jn * normal + jt * tangent;
	}

	// Positional correction
	double corr_mag = std::max(penetration - POS_CORRECT_SLOP, 0.0) * POS_CORRECT_PCT;
	rb.position += corr_mag * normal;

	// Kill inward normal velocity after correction
	double vn = glm::dot(rb.velocity, normal);
	if (vn < 0.0) rb.velocity -= vn * normal;
}

void _rb_rb_collision(physics::RigidbodyData& a, physics::RigidbodyData& b) {
	glm::dvec2 delta = b.position - a.position;
	double dist = glm::length(delta);
	if (dist < 1e-12) return;

	glm::dvec2 normal = delta / dist;
	glm::dvec2 tangent{ normal.y, -normal.x };

	double penetration = (a.radius + b.radius) - dist;
	if (penetration <= 0.0) return;

	glm::dvec2 rv = a.velocity - b.velocity;
	double rel_norm = glm::dot(rv, normal);
	double rel_tan  = glm::dot(rv, tangent);

	double inv_mass_sum = INV_MASS + INV_MASS;
	if (inv_mass_sum <= 0.0) return; // both static

	double e = (std::abs(rel_norm) < REST_THRESHOLD) ? 0.0 : RESTITUTION;

	double jn = 0.0;
	if (rel_norm < 0.0) {
		jn = -(1.0 + e) * rel_norm / inv_mass_sum;
	}

	double jt = -rel_tan / inv_mass_sum;
	double max_friction = FRICTION_COEFF * jn;
	jt = glm::clamp(jt, -max_friction, max_friction);

	glm::dvec2 impulse = jn * normal + jt * tangent;
	a.velocity += impulse * INV_MASS;
	b.velocity -= impulse * INV_MASS;

	// Debug draw impulses
	renderer::DebugLine(a.position, a.position + impulse, {1.0f, 0.0f, 1.0f, 1.0f});
	renderer::DebugLine(b.position, b.position - impulse, {1.0f, 0.0f, 1.0f, 1.0f});

	// Positional correction
	double corr_mag = std::max(penetration - POS_CORRECT_SLOP, 0.0) * POS_CORRECT_PCT / inv_mass_sum;
	glm::dvec2 correction = corr_mag * normal;
	a.position -= correction * INV_MASS;
	b.position += correction * INV_MASS;

	// Kill inward normal velocity after correction
	double vn_post = glm::dot(a.velocity - b.velocity, normal);
	if (vn_post < 0.0) {
		double fix = vn_post / inv_mass_sum;
		a.velocity -= fix * normal * INV_MASS;
		b.velocity += fix * normal * INV_MASS;
	}
}
