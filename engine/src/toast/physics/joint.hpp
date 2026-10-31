/**
 * @file joint.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Joint parameters
 */

#pragma once

#include "body.hpp"

#include <compare>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <variant>

namespace physics {

struct JointID {
	uint32_t slot = std::numeric_limits<uint32_t>::max();
	uint32_t generation = 0;
	auto operator<=>(const JointID&) const = default;
};

enum class JointType : uint8_t {
	pin,
	hinge,
	slider
};

struct PinParams {
	float bias = 0.3f;
	float damping = 1.0f;
	float impulse_clamp = 0.0f;
};

struct HingeParams {
	float bias = 0.3f;
	bool limit_enabled = false;
	float limit_lower = -glm::half_pi<float>();    // radians
	float limit_upper = glm::half_pi<float>();
	float limit_bias = 0.3f;
	bool motor_enabled = false;
	float motor_target_velocity = 0.0f;    // rad/s
	float motor_max_impulse = 1.0f;        // per step
};

// todo: sliderjoint needs to be able to freely rotate
struct SliderParams {
	float bias = 0.3f;
	float linear_limit_lower = -1.0f;
	float linear_limit_upper = 1.0f;
};

using JointParams = std::variant<PinParams, HingeParams, SliderParams>;

struct JointFrame {
	glm::vec3 position = {};
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct JointDescriptor {
	BodyID body_a;
	BodyID body_b;                                          // invalid is the world
	glm::vec3 anchor = {};
	glm::quat frame = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	bool exclude_collision = true;
	JointParams params = PinParams {};
};

struct JointImpulses {
	glm::vec3 point = {};            // hinge and pin anchor
	glm::vec2 perpendicular = {};    // hinge angular, slider linear, along the Y and Z of the frame of body a
	glm::vec3 angular_lock = {};     // slider rotation
	float limit_lower = 0.0f;
	float limit_upper = 0.0f;
	float motor = 0.0f;
};

struct Joint {
	JointDescriptor descriptor;
	JointFrame frame_a;
	JointFrame frame_b;
	JointImpulses impulses;
	float hinge_raw = 0.0f;
	float hinge_angle = 0.0f;
};

struct JointSlot {
	Joint joint;
	uint32_t generation = 1;
	bool occupied = false;
};

}
