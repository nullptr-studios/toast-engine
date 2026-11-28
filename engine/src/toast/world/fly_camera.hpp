/**
 * @file fly_camera.hpp
 * @author Xein
 * @date 25 Nov 2026
 *
 * @brief The editor's fly camera
 */

#pragma once
#include "camera.hpp"
#include "editor_camera.hpp"

#include <memory>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>

namespace toast {

class TOAST_API FlyCamera {
public:
	FlyCamera();
	~FlyCamera();

	FlyCamera(const FlyCamera&) = delete;
	auto operator=(const FlyCamera&) -> FlyCamera& = delete;

	void tick(float dt);

	[[nodiscard]]
	auto flying() const -> bool {
		return m_flying;
	}

private:
	void start();
	void stop();
	void sendMovement() const;

	std::unique_ptr<Camera> m_camera;
	EditorCameraController m_controller;
	event::Listener m_listener;
	bool m_flying = false;
	bool m_looking = false;
	bool m_have_mouse = false;
	float m_mouse_x = 0.0F;
	float m_mouse_y = 0.0F;
	bool m_forward = false;
	bool m_back = false;
	bool m_left = false;
	bool m_right = false;
	bool m_up = false;
	bool m_down = false;
	bool m_boost = false;
};

}
