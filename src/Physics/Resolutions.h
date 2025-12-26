/// @file resolutions.h
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include <Engine/Physics/Line.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>

void _rb_line_collision(physics::RigidbodyData& rb, physics::Line& l) {
	// Projection of the center onto the line axis
	double distance_along_line = glm::dot(rb.position - l.point, l.tangent);

	// Clamp to the segment to get the closest point on the finite line
	double t = glm::clamp(distance_along_line, 0.0, l.length);
	glm::dvec2 closest = l.point + l.tangent * t;

	// Vector from closest point to the circle center
	glm::dvec2 to_center = rb.position - closest;
	double dist = glm::length(to_center);

	// Debug draw
	renderer::DebugCircle(l.point, 0.1f);
	renderer::DebugCircle(rb.position, 0.1f, {1.0f, 1.0f, 0.0f, 1.0f});
	renderer::DebugLine(l.point, rb.position, {1.0f, 0.0f, 1.0f, 0.5f});
	renderer::DebugLine(l.point, l.point + l.normal, {0.0f, 1.0f, 1.0f, 0.5f});
	renderer::DebugLine(l.point, closest, {1.0f, 0.0f, 1.0f, 1.0f});

	// Early out if no overlap with the segment
	double penetration = rb.radius - dist;
	if (penetration <= 0.0) {
		return;
	}

	// Collision normal
	// Fallback if the center lies exactly on the closest point
	glm::dvec2 normal;
	if (dist > 0.0) {
		normal = to_center / dist;
	} else {
		normal = l.normal;
	}

	glm::dvec2 tangent{-normal.y, normal.x};

	double normal_velocity = glm::dot(rb.velocity, normal);
	double tangent_velocity = glm::dot(rb.velocity, tangent);

	// TODO: make these configurable
	constexpr double RESTITUTION = 0.8;
	constexpr double FRICTION = 0.05;

	// Velocity resolution
	if (normal_velocity < 0.0) {
		rb.velocity = (-normal_velocity * RESTITUTION * normal) + (tangent_velocity * (1.0 - FRICTION) * tangent);
	}

	// Positional resolution
	rb.position += normal * penetration;
}
