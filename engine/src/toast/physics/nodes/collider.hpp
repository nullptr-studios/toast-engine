/**
 * @file Collider.hpp
 * @author Xein
 * @date 09 Sep 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "../shape.hpp"

#include <toast/world/node_3d.hpp>

namespace physics {

class Simulator;

class [[ToastNode, Hidden, Interface, Color("Green")]] TOAST_API Collider : public toast::Node3D {
	friend class Simulator;

public:
	[[Reflect]]
	bool disabled = false;

	[[Reflect, Color]]
	glm::vec4 debug_color = glm::vec4(0.0f, 1.0f, 0.251f, 0.5f);    // Matches editor green

	[[Reflect]]
	bool debug_fill = true;

	[[Reflect, Name("Show AABB"), Group("AABB")]]
	bool show_aabb = false;

	[[Reflect, Color, Name("AABB Color"), Group("AABB")]]
	glm::vec4 aabb_color = glm::vec4(1.0f, 0.75f, 0.15f, 0.6f);

	[[Reflect, Name("Fill Shape"), Group("AABB")]]
	bool aabb_fill = false;

private:
	void updateInspectorMessages() override;
	void init();
	void destroy();
	void onEnable();
	void onDisable();
	void drawDebug();

	void assignShape(ShapeID shape) noexcept { m_shape = shape; }

	ShapeID m_shape;
	bool m_debug_visible = false;
};

}
