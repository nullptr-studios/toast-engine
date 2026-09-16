/// @file camera.cpp
/// @author dario
/// @date 7/4/2026

#include "camera.hpp"

#include <toast/renderer/vulkan_renderer.hpp>

#include <cmath>

namespace toast {

Camera::~Camera() {
	if (m_registered_proxy) {
		renderer::unregisterCameraNodeProxy(this);
		m_registered_proxy = false;
	}
	renderer::forgetCamera(this);
}

void Camera::updateInspectorMessages() {
	static const NodeMessage fov_message {
	  .severity = NodeMessage::error,
	  .id = 6,
	  .text = "FOV must be between 0 and 180 degrees",
	};
	static const NodeMessage clipping_message {
	  .severity = NodeMessage::error,
	  .id = 7,
	  .text = "Far must be greater than Near",
	};

	if (std::isfinite(fov) && fov > 0.0f && fov < 180.0f) {
		removeInspectorMessage(fov_message);
	} else {
		addInspectorMessage(fov_message);
	}
	if (std::isfinite(near_plane) && std::isfinite(far_plane) && near_plane > 0.0f && far_plane > near_plane) {
		removeInspectorMessage(clipping_message);
	} else {
		addInspectorMessage(clipping_message);
	}
}

void Camera::setActiveCamera() {
	if (m_owner) {
		m_owner->activateCamera(*this);
		set_as_main.fire(this->box());
	}
}

void Camera::begin() {
	onEnable();

	renderer::registerCameraNodeProxy(this);
	m_registered_proxy = true;
}

void Camera::end() {
	if (m_registered_proxy) {
		renderer::unregisterCameraNodeProxy(this);
		m_registered_proxy = false;
	}
	if (m_owner) {
		m_owner->deactivateCamera(*this);
	}
}

void Camera::onEnable() {
	if (!m_owner) {
		return;
	}
	if (m_is_main_camera) {
		m_owner->setMainCamera(*this);
	} else {
		m_owner->activateCamera(*this);
	}
}

void Camera::onDisable() {
	if (m_owner) {
		m_owner->deactivateCamera(*this);
	}
}

auto Camera::getView() const -> glm::mat4 {
	syncTransform();
	return glm::lookAt(world_position, world_position + forward(), up());
}

auto Camera::getProjection(float aspect) const -> glm::mat4 {
	glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(fov), aspect, near_plane, far_plane);

	proj[1][1] *= -1.0f;

	return proj;
}

auto Camera::screenPointToRay(glm::vec2 screen_px, glm::vec2 viewport_size) const noexcept -> Ray {
	syncTransform();

	if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) {
		return {world_position, forward()};
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

	return {world_position, glm::normalize(glm::vec3(far_point) - glm::vec3(near_point))};
}
}
