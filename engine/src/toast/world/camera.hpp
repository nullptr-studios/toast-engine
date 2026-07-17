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

private:
};
}
