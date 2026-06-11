/// @file Camera.cpp
/// @author dario
/// @date 10/06/2026.

#include "Camera.hpp"

void Camera::SetActiveCamera(bool force) {
	// todo
}

glm::mat4 Camera::getView() const {
	glm::mat4 world(1.0f);

	world = glm::lookAt(position, glm::vec3(0), glm::vec3(0, 0, 1));

	return world;
}

glm::mat4 Camera::getProjection(float aspect) const {
	glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);

	proj[1][1] *= -1.0f;

	return proj;
}
