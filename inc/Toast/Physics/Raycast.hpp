/**
 * @file Raycast.hpp
 * @author Iñaki
 * @date 12/01/2026
 *
 */
#pragma once
#include "glm/vec2.hpp"

namespace toast { class Object; }

namespace physics {

struct RayResult {
	enum Type : uint8_t {
		Collider, Rigidbody, Box
	};

	Type type = Collider;
	glm::vec2 point = {0.0f, 0.0f};
	float distance = 0.0f;
	toast::Object* other = nullptr;
};

auto RayCast(glm::vec2 point, glm::vec2 dir) -> std::optional<RayResult>;

}
