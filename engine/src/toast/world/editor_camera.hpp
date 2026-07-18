/**
 * @file editor_camera.hpp
 * @author Dario
 */

#pragma once

#include "camera.hpp"

#include <toast/events/listener.hpp>

namespace toast {

class TOAST_API EditorCameraController {
public:
	EditorCameraController();

	/// @brief Advances the fly camera and writes the result onto target
	/// @param dt Delta time in seconds
	void tick(float dt, Camera* target);

private:
	event::Listener m_listener;

	bool m_active = false;
	bool m_move_forward = false;
	bool m_move_back = false;
	bool m_move_left = false;
	bool m_move_right = false;
	bool m_move_up = false;
	bool m_move_down = false;
	bool m_boost = false;

	glm::vec3 m_position {0.0f, -5.0f, 2.0f};
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;

	float m_speed = 5.0f;

	static constexpr float k_min_speed = 0.5f;
	static constexpr float k_max_speed = 50.0f;
	static constexpr float k_boost_multiplier = 3.0f;
	static constexpr float k_look_sensitivity = 0.0025f;
	static constexpr float k_pitch_limit = 1.5533f;    // ~89 degrees
};

}
