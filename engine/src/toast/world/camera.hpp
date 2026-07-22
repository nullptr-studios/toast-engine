/**
 * @file camera.hpp
 * @author Xein
 * @date 22 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include "node_3d.hpp"

#include <toast/export.hpp>

namespace toast {
class [[ToastNode, Icon("Camera")]] TOAST_API Camera : public Node3D {
	friend class CameraController;

public:
	Camera() = default;

	~Camera() override = default;

public:
	[[Reflect, Unit("°")]]
	float fov = 75.f;

	[[Reflect, Unit("m")]]
	float near_plane = 0.01f;

	[[Reflect, Unit("m")]]
	float far_plane = 100.f;

	void setActiveCamera();

	[[nodiscard]]
	auto getView() const -> glm::mat4;
	[[nodiscard]]
	auto getProjection(float aspect) const -> glm::mat4;

private:
	void begin();
	void end();
	void onEnable();
	void onDisable();

	[[Reflect, ReadOnly]]
	bool m_is_active = false;

	friend class INodeOwner;
};
}
