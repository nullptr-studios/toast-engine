/**
 * @file body.hpp
 * @author Xein
 * @date 09 Sep 2026
 * @brief Body identifiers, creation data, and copied runtime state
 */

#pragma once

#include <compare>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>

namespace physics {

struct BodyID {
	uint32_t slot = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;
	auto operator<=>(const BodyID&) const = default;
};

enum class BodyType : uint8_t {
	static_body,
	dynamic_body,
	kinematic_body
};

struct BodyDescriptor {
	BodyType type = BodyType::dynamic_body;
	bool allow_sleep = true;
	glm::vec3 position = {};
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 linear_velocity = {};
	glm::vec3 angular_velocity = {};
	float mass = 1.0f;
	float gravity_scale = 1.0f;
};

struct BodyState {
	BodyType type = BodyType::dynamic_body;
	bool awake = true;
	glm::vec3 position = {};
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 previous_position = {};
	glm::quat previous_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 linear_velocity = {};
	glm::vec3 angular_velocity = {};
};

struct Body {
	BodyType type = BodyType::dynamic_body;
	bool enabled = true;
	bool awake = true;
	bool allow_sleep = true;
	float sleep_timer = 0.0f;
	glm::vec3 position = {};
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 previous_position = {};
	glm::quat previous_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 linear_velocity = {};
	glm::vec3 angular_velocity = {};
	float inverse_mass = 1.0f;
	float gravity_scale = 1.0f;
	glm::mat3 inverse_inertia_local = {0.0f};
	glm::mat3 inverse_inertia_world = {0.0f};
	glm::vec3 local_center_of_mass = {};

	[[nodiscard]]
	auto worldCenterOfMass() const -> glm::vec3 {
		return position + rotation * local_center_of_mass;
	}
};

struct BodySlot {
	Body body;
	uint32_t generation = 1;
	bool occupied = false;
};

}
