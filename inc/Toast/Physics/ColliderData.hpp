/// @file ColliderData.hpp
/// @author Xein
/// @date 29 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct ColliderData {
	double friction = 0.4;
	glm::dvec2 worldPosition;

	bool debugNormals = false;
};

}
