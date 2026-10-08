/**
 * @file damage_command.hpp
 * @author Xein
 * @date 18 Sep 2026
 * @brief TODO: Add file description
 */

#pragma once
#include "body.hpp"
#include "physics_settings.hpp"
#include "shape.hpp"

#include <glm/glm.hpp>
#include <optional>

namespace physics {

struct FragmentPush {
	std::optional<glm::vec3> point;
	glm::vec3 direction {};
	float speed = 0.0f;
	float max_impulse = 0.0f;
};

struct DamageCommand {
	ShapeID shape;
	glm::vec3 world_center {};
	float radius = 0.0f;
	float energy = 0.0f;
	BodyID source;

	float shell_voxels = tunables().fracture_shell_voxels;
	std::optional<FragmentPush> push;
};

struct CapsuleSmash {
	float energy = 0.0f;
	glm::vec3 direction {};
	float force = 0.0f;
	float max_speed = 0.0f;
};

}
