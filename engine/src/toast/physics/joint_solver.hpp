/**
 * @file joint_solver.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Sequential impulses of the joints
 */

#pragma once

#include "joint.hpp"

namespace physics::_detail {

struct JointRow {
	glm::vec3 r_a = {};
	glm::vec3 r_b = {};
	glm::vec3 error = {};
	glm::mat3 axes_a = {1.0f};
	glm::mat3 axes_b = {1.0f};
	glm::mat3 inverse_point_mass = {0.0f};
	glm::mat3 inverse_angular_mass = {0.0f};
	glm::vec3 angular_error = {};    // slider
	float position = 0.0f;           // slider
	float angle = 0.0f;              // hinge
	float inverse_dt = 0.0f;
};

[[nodiscard]]
TOAST_API auto makeJoint(const JointDescriptor& descriptor, const Body& body_a, const Body& body_b) -> Joint;

[[nodiscard]]
TOAST_API auto prepareJoint(Joint& joint, const Body& body_a, const Body& body_b, float dt) -> JointRow;

TOAST_API void warmStartJoint(const JointRow& row, const Joint& joint, Body& body_a, Body& body_b);

TOAST_API void solveJoint(const JointRow& row, Joint& joint, Body& body_a, Body& body_b);

struct JointReaction {
	float linear = 0.0f;
	float angular = 0.0f;
};

[[nodiscard]]
TOAST_API auto jointReaction(const Joint& joint) -> JointReaction;

TOAST_API void correctJointPositions(Joint& joint, Body& body_a, Body& body_b);

}
