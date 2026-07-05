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
	float nearPlane = 0.01f;
	float farPlane = 100.f;

	void SetActiveCamera(bool force = true);

	[[nodiscard]]
	glm::mat4 getView() const;
	[[nodiscard]]
	glm::mat4 getProjection(float aspect) const;

private:
};
}
