#include "editor_camera.hpp"

#include "workspace_events.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace toast {

void EditorCameraController::setEnabled(bool enabled) noexcept {
	if (m_enabled == enabled) {
		return;
	}
	m_enabled = enabled;
	if (!m_enabled) {
		// Drop any held state, so a controller that loses focus mid-drag doesn't resume flying in whatever
		// direction the keys were last seen in when it becomes active again
		m_active = false;
		m_move_forward = m_move_back = m_move_left = m_move_right = m_move_up = m_move_down = m_boost = false;
	}
}

EditorCameraController::EditorCameraController() {
	syncOrbitFromPosition();

	m_listener.subscribe<event::EditorCameraFlyMode>([this](const auto& e) {
		if (!m_enabled) {
			return false;
		}
		// only gates looking around, movement follows EditorCameraMoveState on its own
		m_active = e.active;
		return true;
	});

	m_listener.subscribe<event::EditorCameraMoveState>([this](const auto& e) {
		if (!m_enabled) {
			return false;
		}
		m_move_forward = e.forward;
		m_move_back = e.back;
		m_move_left = e.left;
		m_move_right = e.right;
		m_move_up = e.up;
		m_move_down = e.down;
		m_boost = e.boost;
		return true;
	});

	m_listener.subscribe<event::EditorCameraLook>([this](const auto& e) {
		if (!m_enabled) {
			return false;
		}
		if (!m_active) {
			return true;
		}
		applyLook(e.dx, e.dy);
		return true;
	});

	m_listener.subscribe<event::EditorCameraGesture>([this](const auto& e) {
		if (!m_enabled) {
			return false;
		}
		applyPan(e.dx, e.dy);
		applyZoom(e.zoom);
		return true;
	});
}

void EditorCameraController::copyViewFrom(const EditorCameraController& other) noexcept {
	m_position = other.m_position;
	m_yaw = other.m_yaw;
	m_pitch = other.m_pitch;
	m_orbit_elevation = other.m_orbit_elevation;
	m_orbit_radius = other.m_orbit_radius;
	m_speed = other.m_speed;
	m_mode = other.m_mode;
}

void EditorCameraController::configure(event::EditorCameraMode mode, float speed) {
	setMode(mode);
	m_speed = std::clamp(speed, k_min_speed, k_max_speed);
}

void EditorCameraController::setMode(event::EditorCameraMode mode) {
	if (mode == m_mode) {
		return;
	}

	if (mode == event::EditorCameraMode::orbit) {
		syncOrbitFromPosition();
	} else {
		m_pitch = -m_orbit_elevation;
	}
	m_mode = mode;
}

void EditorCameraController::syncOrbitFromPosition() {
	m_orbit_radius = std::clamp(glm::length(m_position), k_min_radius, k_max_radius);
	if (m_orbit_radius > k_min_radius) {
		m_yaw = std::atan2(m_position.x, -m_position.y);
		m_orbit_elevation = std::clamp(std::asin(m_position.z / m_orbit_radius), -k_pitch_limit, k_pitch_limit);
	}
}

auto EditorCameraController::orientation() const -> glm::quat {
	const float pitch = m_mode == event::EditorCameraMode::orbit ? -m_orbit_elevation : m_pitch;
	return glm::angleAxis(m_yaw, Camera::world_up) * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

void EditorCameraController::applyLook(float dx, float dy) {
	m_yaw -= dx * k_look_sensitivity;
	if (m_mode == event::EditorCameraMode::orbit) {
		m_orbit_elevation = std::clamp(m_orbit_elevation + (dy * k_look_sensitivity), -k_pitch_limit, k_pitch_limit);
	} else {
		m_pitch = std::clamp(m_pitch - (dy * k_look_sensitivity), -k_pitch_limit, k_pitch_limit);
	}
}

void EditorCameraController::applyPan(float dx, float dy) {
	if (m_mode == event::EditorCameraMode::orbit) {
		applyLook(dx, dy);
		return;
	}

	const glm::quat rot = orientation();
	const glm::vec3 right = rot * glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 up = rot * Camera::world_up;
	m_position += ((right * -dx) + (up * dy)) * k_gesture_pan_sensitivity * m_speed;
}

void EditorCameraController::applyZoom(float delta) {
	if (delta == 0.0f) {
		return;
	}
	if (m_mode == event::EditorCameraMode::orbit) {
		m_orbit_radius = std::clamp(m_orbit_radius * std::exp(-delta * k_gesture_zoom_sensitivity), k_min_radius, k_max_radius);
		return;
	}

	m_position += (orientation() * Camera::world_forward) * delta * m_speed * k_gesture_zoom_sensitivity;
}

void EditorCameraController::tick(float dt, Camera* target) {
	if (!target) {
		return;
	}

	if (m_mode == event::EditorCameraMode::orbit) {
		const float speed = m_speed * (m_boost ? k_boost_multiplier : 1.0f);
		const float angular_speed = speed / std::max(m_orbit_radius, k_min_radius);
		if (m_move_forward) {
			m_orbit_elevation += angular_speed * dt;
		}
		if (m_move_back) {
			m_orbit_elevation -= angular_speed * dt;
		}
		if (m_move_left) {
			m_yaw -= angular_speed * dt;
		}
		if (m_move_right) {
			m_yaw += angular_speed * dt;
		}
		if (m_move_down) {
			m_orbit_radius += speed * dt;
		}
		if (m_move_up) {
			m_orbit_radius -= speed * dt;
		}
		m_orbit_elevation = std::clamp(m_orbit_elevation, -k_pitch_limit, k_pitch_limit);
		m_orbit_radius = std::clamp(m_orbit_radius, k_min_radius, k_max_radius);

		const float horizontal = std::cos(m_orbit_elevation) * m_orbit_radius;
		m_position = {
		  std::sin(m_yaw) * horizontal,
		  -std::cos(m_yaw) * horizontal,
		  std::sin(m_orbit_elevation) * m_orbit_radius,
		};
		target->world_position = m_position;
		target->world_rotation = orientation();
		target->syncTransform();
		return;
	}

	const glm::quat rot = orientation();
	const glm::vec3 forward = rot * Camera::world_forward;
	const glm::vec3 right = glm::normalize(glm::cross(forward, Camera::world_up));

	glm::vec3 move(0.0f);
	if (m_move_forward) {
		move += forward;
	}
	if (m_move_back) {
		move -= forward;
	}
	if (m_move_right) {
		move += right;
	}
	if (m_move_left) {
		move -= right;
	}
	if (m_move_up) {
		move += Camera::world_up;
	}
	if (m_move_down) {
		move -= Camera::world_up;
	}

	if (glm::length(move) > 0.0001f) {
		move = glm::normalize(move);
		const float speed = m_speed * (m_boost ? k_boost_multiplier : 1.0f);
		m_position += move * speed * dt;
	}

	target->world_position = m_position;
	target->world_rotation = rot;
	target->syncTransform();
}

}
