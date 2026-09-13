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

/// @brief World-space ray,
/// @TODO: Move to some other engine part
struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;
};

class [[ToastNode, Icon("Camera")]] TOAST_API Camera : public Node3D {
	friend class CameraController;

public:
	Camera() = default;

	~Camera() override;

public:
	[[Reflect, Unit("°")]]
	float fov = 75.f;

	[[Reflect, Unit("m")]]
	float near_plane = 0.01f;

	[[Reflect, Unit("m")]]
	float far_plane = 5000.f;

	void setActiveCamera();

	[[nodiscard]]
	auto isMainCamera() const noexcept -> bool {
		return m_is_main_camera;
	}

	[[nodiscard]]
	auto getView() const -> glm::mat4;
	[[nodiscard]]
	auto getProjection(float aspect) const -> glm::mat4;

	/**
	 * @brief Unprojects a viewport-space pixel into a world-space ray
	 * @param screen_px Pixel coordinates, origin top-left, y-down
	 * @param viewport_size Current viewport width/height in pixels
	 */
	[[nodiscard]]
	auto screenPointToRay(glm::vec2 screen_px, glm::vec2 viewport_size) const noexcept -> Ray;

private:
	void begin();
	void end();
	void onEnable();
	void onDisable();

	/// When set, this camera takes over as the owner's active camera on load/enable, even if another camera is already active
	[[Reflect, Name("Is Main Camera?")]]
	bool m_is_main_camera = false;

	[[Reflect, ReadOnly]]
	bool m_is_active = false;

	bool m_registered_proxy = false;

	friend class INodeOwner;
};
}
