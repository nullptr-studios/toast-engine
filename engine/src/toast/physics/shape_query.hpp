/**
 * @file shape_query.hpp
 * @author Dario
 * @date 23 Sep 2026
 */

#pragma once

#include "body.hpp"
#include "shape.hpp"

#include <glm/glm.hpp>

namespace physics {

/// normal points out of the obstacle toward the query shape
struct QueryContact {
	BodyID body;
	ShapeID shape;
	glm::vec3 normal = {};
	glm::vec3 point = {};
	float penetration = 0.0f;
};

struct SweepHit {
	bool hit = false;
	float fraction = 1.0f;
	glm::vec3 position = {};
	glm::vec3 normal = {};
	glm::vec3 point = {};
	BodyID body;
	ShapeID shape;
};

}
