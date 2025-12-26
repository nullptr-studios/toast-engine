/// @file Line.hpp
/// @author Xein
/// @date 26 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Line {
	glm::dvec2 point;
	glm::dvec2 tangent;
	glm::dvec2 normal;
	double length;
};

struct RigidbodyData {
	glm::dvec2 position;
	glm::dvec2 velocity;
	double radius;
};

}
