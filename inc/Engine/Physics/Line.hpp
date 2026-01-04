/// @file Line.hpp
/// @author Xein
/// @date 2 Jan 2026

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Line {
	glm::vec2 p1;
	glm::vec2 p2;
	glm::dvec2 normal;
	glm::dvec2 tangent;
	double length;
};

}
