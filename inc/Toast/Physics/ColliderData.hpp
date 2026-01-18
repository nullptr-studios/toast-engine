/// @file ColliderData.hpp
/// @author Xein
/// @date 29 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace toast { class Object; }

namespace physics {

struct ColliderData {
	double friction = 0.4;
	glm::vec2 worldPosition;
	toast::Object* parent;

	bool debugNormals = false;
};

}
