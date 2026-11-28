#include "fly_camera.hpp"

#include "workspace_events.hpp"

#include <SDL3/SDL_keycode.h>
#include <toast/params/registry.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/window/window_events.hpp>

namespace toast {
namespace {

constexpr int k_right_button = 3;

auto parameterOn() -> bool {
	const auto* param = params::Registry::get().find("renderer.editor_camera");
	if (param == nullptr) {
		return false;
	}
	const auto value = param->snapshot();
	if (const auto* scalar = std::get_if<params::Scalar>(&value)) {
		if (const auto* on = std::get_if<bool>(scalar)) {
			return *on;
		}
	}
	return false;
}

}

FlyCamera::FlyCamera() : m_camera(std::make_unique<Camera>()) {
	m_listener.subscribe<event::WindowKey>([this](const event::WindowKey& e) {
		if (!m_flying) {
			return false;
		}
		const bool down = e.action != event::window_input_released;
		bool* target = nullptr;
		switch (e.key) {
			case SDLK_W: target = &m_forward; break;
			case SDLK_S: target = &m_back; break;
			case SDLK_A: target = &m_left; break;
			case SDLK_D: target = &m_right; break;
			case SDLK_E: target = &m_up; break;
			case SDLK_Q: target = &m_down; break;
			case SDLK_LSHIFT:
			case SDLK_RSHIFT: target = &m_boost; break;
			default: return false;
		}
		*target = down;
		sendMovement();
		return true;
	});

	m_listener.subscribe<event::WindowMouseButton>([this](const event::WindowMouseButton& e) {
		if (!m_flying || e.button != k_right_button) {
			return false;
		}
		m_looking = e.action != event::window_input_released;
		event::send<event::EditorCameraFlyMode>(m_looking);
		return true;
	});

	m_listener.subscribe<event::WindowMousePosition>([this](const event::WindowMousePosition& e) {
		const float dx = e.x - m_mouse_x;
		const float dy = e.y - m_mouse_y;
		const bool had_mouse = m_have_mouse;
		m_mouse_x = e.x;
		m_mouse_y = e.y;
		m_have_mouse = true;
		if (!m_flying || !m_looking || !had_mouse) {
			return false;
		}
		event::send<event::EditorCameraLook>(dx, dy);
		return true;
	});
}

FlyCamera::~FlyCamera() {
	stop();
}

void FlyCamera::sendMovement() const {
	event::send<event::EditorCameraMoveState>(m_forward, m_back, m_left, m_right, m_up, m_down, m_boost);
}

void FlyCamera::start() {
	m_flying = true;
	m_forward = m_back = m_left = m_right = m_up = m_down = m_boost = false;
	m_looking = false;

	if (const Camera* current = renderer::getActiveCamera(); current != nullptr && current != m_camera.get()) {
		m_controller.placeAt(current->world_position, current->world_rotation);
	}
	m_controller.setEnabled(true);
	m_controller.tick(0.0F, m_camera.get());
	renderer::setCameraOverride(m_camera.get());
}

void FlyCamera::stop() {
	if (!m_flying) {
		return;
	}
	m_flying = false;
	m_controller.setEnabled(false);
	renderer::setCameraOverride(nullptr);
}

void FlyCamera::tick(float dt) {
	const bool wanted = parameterOn();
	if (wanted && !m_flying) {
		start();
	} else if (!wanted && m_flying) {
		stop();
	}
	if (m_flying) {
		m_controller.tick(dt, m_camera.get());
	}
}

}
