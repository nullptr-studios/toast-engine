#include "camera_controller.hpp"

#include "camera.hpp"
#include "camera_events.hpp"

#include <algorithm>
#include <cmath>
#include <toast/time.hpp>
#include <toast/world/node_3d.hpp>

namespace {

constexpr float transition_epsilon = 0.0001f;

auto cubicHermite(float p0, float tangent0, float p1, float tangent1, float t) -> float {
	const float t2 = t * t;
	const float t3 = t2 * t;
	return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * tangent0 + (-2.0f * t3 + 3.0f * t2) * p1 +
	       (t3 - t2) * tangent1;
}

auto easeTransition(float progress, float ease_in, float ease_out) -> float {
	const float in_portion = std::clamp(ease_in, 0.0f, 50.0f) / 100.0f;
	const float out_portion = std::clamp(ease_out, 0.0f, 50.0f) / 100.0f;
	const float middle_speed = 1.0f / (1.0f - (in_portion + out_portion) * 0.5f);
	const float in_distance = middle_speed * in_portion * 0.5f;
	const float out_distance = middle_speed * out_portion * 0.5f;

	if (in_portion > transition_epsilon && progress < in_portion) {
		const float t = progress / in_portion;
		return cubicHermite(0.0f, 0.0f, in_distance, middle_speed * in_portion, t);
	}
	if (out_portion > transition_epsilon && progress > 1.0f - out_portion) {
		const float t = (progress - (1.0f - out_portion)) / out_portion;
		return cubicHermite(1.0f - out_distance, middle_speed * out_portion, 1.0f, 0.0f, t);
	}

	return in_distance + middle_speed * (progress - in_portion);
}

auto sphericalPosition(glm::vec3 start, glm::vec3 end, glm::vec3 center, float progress) -> glm::vec3 {
	const glm::vec3 start_offset = start - center;
	const glm::vec3 end_offset = end - center;
	const float start_radius = glm::length(start_offset);
	const float end_radius = glm::length(end_offset);

	if (start_radius < transition_epsilon || end_radius < transition_epsilon) {
		return glm::mix(start, end, progress);
	}

	const glm::vec3 start_direction = start_offset / start_radius;
	const glm::vec3 end_direction = end_offset / end_radius;
	const float cos_angle = std::clamp(glm::dot(start_direction, end_direction), -1.0f, 1.0f);
	const float angle = std::acos(cos_angle);
	const float sin_angle = std::sin(angle);

	if (std::abs(sin_angle) < transition_epsilon) {
		return glm::mix(start, end, progress);
	}

	const glm::vec3 direction =
	    (std::sin((1.0f - progress) * angle) * start_direction + std::sin(progress * angle) * end_direction) / sin_angle;
	const float radius = glm::mix(start_radius, end_radius, progress);
	return center + direction * radius;
}

}

namespace toast {
void CameraController::setActiveCamera(Box<Camera> c) {
	if (m_cameras.empty() || !c.exists()) {
		return;
	}

	const auto it = std::find_if(m_cameras.begin(), m_cameras.end(), [&c](const Box<Camera>& camera) {
		return camera.exists() && camera.rid() == c.rid();
	});

	if (it == m_cameras.end()) {
		TOAST_WARN("World", "Camera {} ({}) not found", c->name(), c->uid());
		return;
	}

	const uint8_t selected_index = static_cast<uint8_t>(std::distance(m_cameras.begin(), it));
	if (m_active_camera_index == selected_index) {
		return;
	}

	Box<Camera> outgoing_camera = getActiveCamera();
	bool has_outgoing_camera = false;
	glm::vec3 transition_start = c->world_position;
	if (m_transition_active && m_transition_camera.exists()) {
		transition_start = m_transition_camera->world_position;
		stopTransition(true);
		has_outgoing_camera = true;
	} else if (outgoing_camera.exists()) {
		transition_start = outgoing_camera->world_position;
		has_outgoing_camera = true;
	}
	if (outgoing_camera.exists()) {
		outgoing_camera->m_is_active = false;
	}

	m_active_camera_index = selected_index;
	m_active_camera_name = std::string(c->name());
	c->m_is_active = true;
	if (m_owner) {
		m_owner->applyActiveCamera();
	}

	if (!do_transition || transition_time <= 0.0f || !has_outgoing_camera) {
		return;
	}

	m_transition_camera = c;
	m_transition_start = transition_start;
	m_transition_end = c->world_position;
	m_transition_center = glm::vec3(0.0f);
	if (Box<Node3D> target = look_target.as<Node3D>(); target.exists()) {
		m_transition_center = target->world_position;
	}
	m_transition_elapsed = 0.0f;
	m_transition_duration = transition_time;
	m_transition_ease_in = ease_in;
	m_transition_ease_out = ease_out;
	m_transition_type = transition_type;
	m_transition_active = true;
	c->world_position = m_transition_start;
	c->syncTransform();
}

void CameraController::setActiveCamera(UID uid) {
	const auto it = std::find_if(m_cameras.begin(), m_cameras.end(), [&uid](const Box<Camera>& camera) {
		return camera.exists() && camera->uid() == uid;
	});
	if (it == m_cameras.end()) {
		TOAST_WARN("World", "Camera {} not found", uid);
		return;
	}
	setActiveCamera(*it);
}

void CameraController::setActiveCamera(std::string_view name) {
	const auto it = std::find_if(m_cameras.begin(), m_cameras.end(), [name](const Box<Camera>& camera) {
		return camera.exists() && camera->name() == name;
	});
	if (it == m_cameras.end()) {
		TOAST_WARN("World", "Camera '{}' not found", name);
		return;
	}
	setActiveCamera(*it);
}

auto CameraController::getActiveCamera() -> Box<Camera> {
	if (m_active_camera_index == no_active_camera || m_active_camera_index >= m_cameras.size()) {
		return {};
	}
	return m_cameras[m_active_camera_index];
}

auto CameraController::getActiveCameraIndex() -> uint8_t {
	if (m_active_camera_index == no_active_camera || m_active_camera_index >= m_cameras.size()) {
		return no_active_camera;
	}
	return m_active_camera_index;
}

void CameraController::prev() {
	if (m_cameras.empty()) {
		return;
	}

	const size_t current = m_active_camera_index < m_cameras.size() ? m_active_camera_index : 0;
	const size_t previous = (current + m_cameras.size() - 1) % m_cameras.size();
	setActiveCamera(m_cameras[previous]);
}

void CameraController::next() {
	if (m_cameras.empty()) {
		return;
	}

	const size_t current = m_active_camera_index < m_cameras.size() ? m_active_camera_index : m_cameras.size() - 1;
	const size_t following = (current + 1) % m_cameras.size();
	setActiveCamera(m_cameras[following]);
}

void CameraController::addCamera(Camera& camera) {
	const auto it = std::find_if(m_cameras.begin(), m_cameras.end(), [&camera](const Box<Camera>& entry) {
		return entry.exists() && entry.rid() == camera.box().rid();
	});
	if (it != m_cameras.end()) {
		return;
	}

	camera.addDependsOn(*this);
	m_cameras.emplace_back(camera.box().as<Camera>());
	if (!getActiveCamera().exists()) {
		setActiveCamera(m_cameras.back());
	}
}

void CameraController::removeCamera(Camera& camera) {
	const auto it = std::find_if(m_cameras.begin(), m_cameras.end(), [&camera](const Box<Camera>& entry) {
		return entry.exists() && entry.rid() == camera.box().rid();
	});
	if (it == m_cameras.end()) {
		return;
	}

	const size_t removed_index = static_cast<size_t>(std::distance(m_cameras.begin(), it));
	const bool was_active = m_active_camera_index == removed_index;
	Box<Camera> replacement;
	bool replacement_after_removed = false;
	if (was_active && m_cameras.size() > 1) {
		replacement_after_removed = removed_index + 1 < m_cameras.size();
		const size_t replacement_index = replacement_after_removed ? removed_index + 1 : removed_index - 1;
		replacement = m_cameras[replacement_index];
	}
	camera.removeDependsOn(*this);

	if (was_active) {
		if (replacement.exists()) {
			setActiveCamera(replacement);
		} else {
			stopTransition(true);
			camera.m_is_active = false;
			m_active_camera_index = no_active_camera;
			m_active_camera_name.clear();
		}
	} else if (m_active_camera_index != no_active_camera && m_active_camera_index > removed_index) {
		--m_active_camera_index;
	}

	m_cameras.erase(it);
	if (was_active && replacement_after_removed) {
		--m_active_camera_index;
	} else if (was_active && m_owner) {
		m_owner->applyActiveCamera();
	}
}

void CameraController::clearCameras() {
	while (!m_cameras.empty()) {
		Box<Camera> camera = m_cameras.back();
		if (camera.exists()) {
			removeCamera(*camera);
		} else {
			m_cameras.pop_back();
		}
	}
}

void CameraController::init() {
	listener().subscribe<event::SetActiveCamera>([this](auto& e) {
		if (e.camera.exists()) {
			setActiveCamera(e.camera);
		}
	});
}

void CameraController::begin() {
	if (m_owner) {
		m_owner->activateCameraController(*this);
	}
}

void CameraController::end() {
	stopTransition(true);
	clearCameras();
	if (m_owner) {
		m_owner->deactivateCameraController(*this);
	}
}

void CameraController::onEnable() {
	if (m_owner) {
		m_owner->activateCameraController(*this);
	}
}

void CameraController::onDisable() {
	stopTransition(true);
	clearCameras();
	if (m_owner) {
		m_owner->deactivateCameraController(*this);
	}
}

void CameraController::tick() {
	if (!m_transition_active || !m_transition_camera.exists()) {
		return;
	}

	m_transition_elapsed += static_cast<float>(Time::delta());
	const float progress = std::clamp(m_transition_elapsed / m_transition_duration, 0.0f, 1.0f);
	const float eased_progress = easeTransition(progress, m_transition_ease_in, m_transition_ease_out);

	if (m_transition_type == TransitionType::spherical) {
		m_transition_camera->world_position =
		    sphericalPosition(m_transition_start, m_transition_end, m_transition_center, eased_progress);
		m_transition_camera->syncTransform();
		m_transition_camera->lookAt(m_transition_center);
	} else {
		m_transition_camera->world_position = glm::mix(m_transition_start, m_transition_end, eased_progress);
	}

	if (progress >= 1.0f) {
		stopTransition(true);
	}
}

void CameraController::stopTransition(bool restore_camera_position) {
	if (!m_transition_active) {
		return;
	}

	if (restore_camera_position && m_transition_camera.exists()) {
		m_transition_camera->world_position = m_transition_end;
		m_transition_camera->syncTransform();
		if (m_transition_type == TransitionType::spherical) {
			m_transition_camera->lookAt(m_transition_center);
		}
	}
	m_transition_active = false;
	m_transition_elapsed = 0.0f;
	m_transition_duration = 0.0f;
	m_transition_camera = {};
}
}
