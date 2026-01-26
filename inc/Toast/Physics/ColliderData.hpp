/// @file ColliderData.hpp
/// @author Xein
/// @date 29 Dec 2025

#pragma once
#include <glm/glm.hpp>

enum class ColliderFlags : uint8_t;

namespace toast { class Object; }

namespace physics {

struct ColliderData {
	double friction = 0.4;
	glm::vec2 worldPosition;
	toast::Object* parent;
	ColliderFlags flags{};
	bool debugNormals = false;
};

}
