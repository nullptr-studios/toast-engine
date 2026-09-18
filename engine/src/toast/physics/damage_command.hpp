/**
 * @file damage_command.hpp
 * @author Xein
 * @date 18 Sep 2026
 * @brief TODO: Add file description
 */

#pragma once
#include "body.hpp"
#include "shape.hpp"

#include <glm/glm.hpp>

namespace physics {

struct DamageCommand {
	ShapeID shape;
	glm::vec3 world_center {};
	float radius = 0.0f;
	float energy = 0.0f;
	BodyID source;
};

}
