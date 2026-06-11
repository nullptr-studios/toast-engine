/// @file Camera.hpp
/// @author dario
/// @date 10/06/2026.

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class Camera /*INHERIT FROM NODE3D*/ {
public:
	// TODO: transform
	glm::vec3 position;

	glm::vec3 rotation;

	float fov = 75.f;
	float nearPlane = 0.01f;
	float farPlane = 100.f;

	void SetActiveCamera(bool force = true);

	glm::mat4 getView() const;
	glm::mat4 getProjection(float aspect) const;
};
