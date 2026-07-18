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
public:
	Camera() = default;

	~Camera() override = default;

public:
	float fov = 75.f;
	float near_plane = 0.01f;
	float far_plane = 5000.f;

	void setActiveCamera(bool force = true);

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
};
}
