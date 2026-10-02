/**
 * @file trigger.hpp
 * @author Xein
 * @date 2 Oct 2026
 * @brief Overlap volume that reports bodies entering and leaving it
 */

#pragma once
#include "../body.hpp"

#include <span>
#include <toast/events/signals.hpp>
#include <toast/world/node_3d.hpp>
#include <vector>

namespace physics {

/**
 * For custom logic when something enters/exists use either the signal or create a
 * onEnter(other)/onExit(other) function both on a Lua script or on an inherited class
 */
class TOAST_API [[ToastNode, Icon("Area"), Color("Green")]] Trigger : public toast::Node3D {
	friend class Simulator;

public:
	signals::Signal<toast::Box<toast::Node3D>> on_enter;
	signals::Signal<toast::Box<toast::Node3D>> on_exit;

	[[Reflect]]
	auto getCurrentCollisions() const -> std::vector<toast::Box<toast::Node3D>>;

	auto isInside(const toast::Node3D& node) const -> bool;

	[[Reflect]]
	auto empty() const -> bool;

	/// Color of the collider while something is inside
	[[Reflect, Color]]
	glm::vec4 debug_color = glm::vec4(0.0f, 1.0f, 0.251f, 0.5f);

	/// Color of the collider while nothing is inside
	[[Reflect, Color]]
	glm::vec4 empty_color = glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);

private:
	struct Overlap {
		BodyID body;
		toast::Box<toast::Node3D> node;
	};

	void begin();
	void end();
	void onDisable();
	void updateOverlaps(std::span<const BodyID> bodies);
	void applyDebugColor();

	void updateInspectorMessages() override;

	std::vector<Overlap> m_current;
};

}
