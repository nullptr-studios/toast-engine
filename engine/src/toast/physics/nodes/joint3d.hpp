/**
 * @file joint3d.hpp
 * @author Xein
 * @date 31 Oct 2026
 * @brief Interface for all joint types
 */

#pragma once
#include "../joint.hpp"

#include <array>
#include <toast/world/gizmo_layout.hpp>
#include <toast/world/node_3d.hpp>
#include <utility>

namespace physics {

class Simulator;

struct LimitHandle {
	toast::GizmoHandle handle = toast::GizmoHandle::none;
	glm::vec3 local {0.0f};
};

using LimitHandles = std::pair<std::array<LimitHandle, 2>, uint32_t>;

class [[ToastNode, Hidden, Interface, Color("Green")]] TOAST_API Joint3D : public toast::Node3D {
	friend class Simulator;

public:
	/// The joint broke because of what it held or because a script asked for it
	signals::Signal<> joint_broken;

	[[Reflect]]
	toast::Box<toast::Node3D> node_a;

	[[Reflect]]
	toast::Box<toast::Node3D> node_b;

	/// The two bodies do not collide with each other while the joint holds them
	[[Reflect]]
	bool exclude_nodes_from_collision = true;

	[[Reflect, ReadOnly]]
	bool broken = false;

	[[Reflect]]
	bool draw_debug = true;

	/// 0 never breaks
	[[Reflect, Group("Breaking"), Unit("N")]]
	float break_force = 0.0f;

	[[Reflect, Group("Breaking"), Unit("N•m")]]
	float break_torque = 0.0f;

	[[Reflect, Group("Breaking"), ReadOnly, Unit("N")]]
	float current_force = 0.0f;

	[[Reflect, Group("Breaking"), ReadOnly, Unit("N•m")]]
	float current_torque = 0.0f;

	/// Lets go when the voxels around its end in a voxel body are destroyed
	[[Reflect, Group("Voxel Anchor")]]
	bool break_when_anchor_destroyed = true;

	/// How many voxels each way around the end are looked at
	[[Reflect, Group("Voxel Anchor"), Range(1, 8)]]
	int anchor_radius_voxels = 2;

	/// Percentage that needs to be kept for it to still be alive
	[[Reflect, Group("Voxel Anchor"), Range(0.0, 1.0)]]
	float anchor_min_remaining = 0.5f;

	[[Reflect]]
	void breakJoint();

	[[Reflect]]
	void repair();

	[[nodiscard]]
	auto frameRotation() const -> glm::quat;

	[[nodiscard]]
	virtual auto makeParams() const -> JointParams {
		return PinParams {};
	}

	[[nodiscard]]
	virtual auto limitHandles() const -> LimitHandles {
		return {{}, 0};
	}

	[[nodiscard]]
	virtual auto limitsAreAngles() const -> bool {
		return false;
	}

	[[nodiscard]]
	virtual auto limitValue(toast::GizmoHandle handle) const -> float {
		return 0.0f;
	}

	virtual void setLimitValue(toast::GizmoHandle handle, float value) { }

	[[nodiscard]]
	virtual auto limitField(toast::GizmoHandle handle) const -> const char* {
		return nullptr;
	}

protected:
	virtual void drawGizmo(const glm::vec3& origin, const glm::mat3& axes, const glm::vec4& color) const { }

	virtual void applyState(const JointState& state) { }

	void updateInspectorMessages() override;

private:
	void init();
	void destroy();
	void begin();
	void end();
	void drawDebug();
	void publishState(const JointState& state);
	void markBroken();

	bool m_registered = false;
};

}
