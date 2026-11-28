/**
 * @file editor_camera.hpp
 * @author Dario
 */

#pragma once

#include "camera.hpp"
#include "editor_params.hpp"
#include "workspace_events.hpp"

#include <toast/events/listener.hpp>

namespace toast {

class TOAST_API EditorCameraController {
public:
	EditorCameraController();

	/// @brief Advances the fly camera and writes the result onto target
	/// @param dt Delta time in seconds
	void tick(float dt, Camera* target);

	/**
	 * @brief Enables or disables input handling for this controller
	 *
	 * Every open Workspace owns a controller and they all subscribe to the same global input events, so
	 * without this a fly-camera drag in one workspace would silently move and rotate every other
	 * workspace's camera too - the drift only becoming visible on switching tabs. Only the workspace being
	 * looked through should consume input
	 */
	void setEnabled(bool enabled) noexcept;
	void configure(event::EditorCameraMode mode, float speed, bool horizontal_movement = false);

	void copyViewFrom(const EditorCameraController& other) noexcept;

	void placeAt(glm::vec3 position, const glm::quat& rotation) noexcept;

	[[nodiscard]]
	auto mode() const noexcept -> event::EditorCameraMode {
		return m_mode;
	}

	[[nodiscard]]
	auto speed() const noexcept -> float {
		return m_speed;
	}

	[[nodiscard]]
	auto position() const noexcept -> glm::vec3 {
		return m_position;
	}

	[[nodiscard]]
	auto orbitRadius() const noexcept -> float {
		return m_orbit_radius;
	}

	[[nodiscard]]
	auto orbitElevation() const noexcept -> float {
		return m_orbit_elevation;
	}

private:
	event::Listener m_listener;

	bool m_enabled = false;
	bool m_active = false;
	bool m_move_forward = false;
	bool m_move_back = false;
	bool m_move_left = false;
	bool m_move_right = false;
	bool m_move_up = false;
	bool m_move_down = false;
	bool m_boost = false;

	glm::vec3 m_position {0.0f, -10.0f, 10.0f};
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;
	float m_orbit_elevation = 0.0f;
	float m_orbit_radius = 0.0f;

	float m_speed = editorParams().camera_default_speed;
	bool m_horizontal_movement = false;
	event::EditorCameraMode m_mode = event::EditorCameraMode::free;

	static constexpr float k_pitch_limit = 1.5533f;    // ~89 degrees

	void setMode(event::EditorCameraMode mode);
	void syncOrbitFromPosition();
	void applyLook(float dx, float dy);
	void applyPan(float dx, float dy);
	void applyZoom(float delta);

	[[nodiscard]]
	auto orientation() const -> glm::quat;
};

}
