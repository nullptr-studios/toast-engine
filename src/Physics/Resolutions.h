/// @file resolutions.h
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include "Engine/Renderer/DebugDrawLayer.hpp"
#include <Engine/Physics/Line.hpp>
#include <Engine/Physics/Rigidbody.hpp>

void _rb_line_collision(physics::RigidbodyData& rb, physics::Line& l) {
	// calculate normal of the segment
	double distance_along_line = glm::dot(rb.position - l.point, l.tangent);
	renderer::DebugCircle(l.point, 0.1f);
	renderer::DebugCircle(rb.position, 0.1f, {1.0f, 1.0f, 0.0f, 1.0f});
	glm::dvec2 tan2 = l.tangent + glm::dvec2{0.0f, 1.0f};
	renderer::DebugLine(l.point, l.point + (l.tangent * distance_along_line), {1.0f, 0.0f, 1.0f, 1.0f});

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

	renderer::DebugLine(closest, closest + normal, {0.0f, 1.0f, 1.0f, 0.5f});

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
