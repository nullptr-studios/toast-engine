/// @file camera.cpp
/// @author dario
/// @date 7/4/2026.

#include "camera.hpp"

#include "toast/renderer/vulkan_renderer.hpp"

namespace toast {
void Camera::setActiveCamera(bool force) {
	renderer::VulkanRenderer::instance->setActiveCamera(this);
}

auto Camera::getView() const -> glm::mat4 {
	glm::mat4 world(1.0f);

	world = glm::lookAt(worldPos(), glm::vec3(0), glm::vec3(0, 0, 1));

	return world;
}

auto Camera::getProjection(float aspect) const -> glm::mat4 {
	glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);

	proj[1][1] *= -1.0f;

	return proj;
}
}
