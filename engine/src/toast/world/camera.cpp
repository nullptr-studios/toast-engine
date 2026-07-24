/// @file camera.cpp
/// @author dario
/// @date 7/4/2026.

#include "camera.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {
void Camera::setActiveCamera(bool force) {
	renderer::VulkanRenderer::instance->setActiveCamera(this);
}

auto Camera::getView() const -> glm::mat4 {
	syncTransform();

	glm::vec3 target(0.0f);
	glm::vec3 delta = target - world_position;
	if (glm::length(delta) < 0.0001f) {
		target = world_position + forward();
		delta = target - world_position;
	}
	const glm::vec3 direction = glm::normalize(delta);
	glm::vec3 up(0.0f, 0.0f, 1.0f);
	if (glm::abs(glm::dot(direction, up)) > 0.999f) {
		up = glm::vec3(0.0f, 1.0f, 0.0f);
	}
	return glm::lookAt(world_position, target, up);
}

auto Camera::getProjection(float aspect) const -> glm::mat4 {
	glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);

	proj[1][1] *= -1.0f;

	return proj;
}
}
