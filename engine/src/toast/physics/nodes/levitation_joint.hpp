/**
 * @file levitation_joint.hpp
 * @author Xein
 * @date 21 Nov 2026
 * @brief Holds a body floating around a point like a magnet
 */

#pragma once
#include "joint3d.hpp"

namespace physics {

class [[ToastNode, Icon("SliderJoint"), Color("Green")]] TOAST_API LevitationJoint : public physics::Joint3D {
public:
	signals::Signal<> released;
	signals::Signal<> captured;

	/// How fast the hover settles
	[[Reflect, Group("Hover"), Unit("Hz"), Range(0.0, 20.0)]]
	float stiffness_hz = 2.0f;

	[[Reflect, Group("Hover"), Range(0.0, 2.0)]]
	float damping_ratio = 0.7f;

	/// The strongest pull of the hover
	[[Reflect, Group("Hover"), Unit("N")]]
	float max_force = 100.0f;

	/// A body this close to the point is picked up again
	[[Reflect, Group("Zone"), Unit("m")]]
	float catch_radius = 0.5f;

	/// A body this far from the point is let go
	[[Reflect, Group("Zone"), Unit("m")]]
	float release_radius = 1.0f;

	[[Reflect, Group("Rotation")]]
	bool rotation_free_x = true;

	[[Reflect, Group("Rotation")]]
	bool rotation_free_y = true;

	[[Reflect, Group("Rotation")]]
	bool rotation_free_z = true;

	/// How fast the free axes lose their spin
	[[Reflect, Group("Rotation"), Unit("1/s")]]
	float angular_damping = 1.0f;

	[[Reflect, Group("State"), ReadOnly]]
	bool levitating = true;

	[[Reflect, Group("State"), ReadOnly, Unit("m")]]
	float current_offset = 0.0f;

	[[nodiscard]]
	auto makeParams() const -> JointParams override;

protected:
	void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const override;

	void applyState(const JointState& state) override;
	void updateInspectorMessages() override;
};

}
