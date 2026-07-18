#include "editor_camera.hpp"

#include "workspace_events.hpp"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace toast {

EditorCameraController::EditorCameraController() {
	m_listener.subscribe<event::EditorCameraFlyMode>([this](const auto& e) {
		m_active = e.active;
		if (!m_active) {
			m_move_forward = m_move_back = m_move_left = m_move_right = m_move_up = m_move_down = m_boost = false;
		}
		return true;
	});

	m_listener.subscribe<event::EditorCameraMoveState>([this](const auto& e) {
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
		if (!m_active) {
			return true;
		}
		m_yaw -= e.dx * k_look_sensitivity;
		m_pitch = std::clamp(m_pitch - (e.dy * k_look_sensitivity), -k_pitch_limit, k_pitch_limit);
		return true;
	});

	m_listener.subscribe<event::EditorCameraSpeedScroll>([this](const auto& e) {
		m_speed = std::clamp(m_speed * (1.0f + e.delta * 0.1f), k_min_speed, k_max_speed);
		return true;
	});
}

void EditorCameraController::tick(float dt, Camera* target) {
	if (!target) {
		return;
	}

	const glm::quat rot = glm::angleAxis(m_yaw, Camera::world_up) * glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
	const glm::vec3 forward = rot * Camera::world_forward;
	const glm::vec3 right = glm::normalize(glm::cross(forward, Camera::world_up));

	if (m_active) {
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
	}

	target->worldPos(m_position);
	target->worldRotQuat(rot);
}

}
