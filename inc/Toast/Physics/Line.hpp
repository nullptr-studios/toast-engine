/// @file Line.hpp
/// @author Xein
/// @date 2 Jan 2026

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Line {
	glm::dvec2 p1;
	glm::dvec2 p2;
	glm::dvec2 normal;
	glm::dvec2 tangent;
	double length;
};

}
