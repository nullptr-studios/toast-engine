/// @file Camera.hpp
/// @author dario
/// @date 10/06/2026.

#pragma once

#include "toast/world/node_3d.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class [[ToastNode]] TOAST_API Camera : public toast::Node3D {
public:
	float fov = 75.f;
	float nearPlane = 0.01f;
	float farPlane = 100.f;

	void SetActiveCamera(bool force = true);

	[[nodiscard]]
	glm::mat4 getView() const;
	[[nodiscard]]
	glm::mat4 getProjection(float aspect) const;
};
