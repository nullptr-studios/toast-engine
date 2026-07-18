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
	return glm::lookAt(worldPos(), worldPos() + forward(), up());
}

auto Camera::getProjection(float aspect) const -> glm::mat4 {
	glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);

	proj[1][1] *= -1.0f;

	return proj;
}

auto Camera::screenPointToRay(glm::vec2 screen_px, glm::vec2 viewport_size) const noexcept -> Ray {
	if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) {
		return {worldPos(), forward()};
	}

	const float aspect = viewport_size.x / viewport_size.y;

	// screen (y-down) -> NDC ([-1,1])
	const glm::vec2 ndc {
	  ((2.0f * screen_px.x) / viewport_size.x) - 1.0f,
	  ((2.0f * screen_px.y) / viewport_size.y) - 1.0f,
	};

	const glm::mat4 inv_view_proj = glm::inverse(getProjection(aspect) * getView());

	glm::vec4 near_point = inv_view_proj * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
	glm::vec4 far_point = inv_view_proj * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
	near_point /= near_point.w;
	far_point /= far_point.w;

	return {worldPos(), glm::normalize(glm::vec3(far_point) - glm::vec3(near_point))};
}
}
