#include "camera.hpp"
#include "workspace.hpp"
#include "workspace_events.hpp"

#include <cmath>
#include <toast/engine.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/window/window_events.hpp>

namespace toast {

namespace {

constexpr float k_click_distance = 4.0f;
constexpr float k_max_ground_distance = 2000.0f;
constexpr int k_sdl_shift = 0x0003;
constexpr int k_key_escape = 27;
constexpr int k_key_backspace = 8;
constexpr int k_key_delete = 127;

/// SDL_SCANCODE_MASK | scancode
constexpr int k_key_left_shift = (1 << 30) | 225;
constexpr int k_key_right_shift = (1 << 30) | 229;

}

auto Workspace::rulerPoint() -> std::optional<glm::vec3> {
	std::optional<glm::vec3> point;
	if (const std::optional<SceneHit> hit = pickSceneHit()) {
		point = hit->position;
	} else if (Camera* camera = renderer::getActiveCamera()) {
		const auto extent = renderer::getOutputTarget().getExtent();
		const Ray ray =
		    camera->screenPointToRay(m_gizmo_mouse_pos, {static_cast<float>(extent.width), static_cast<float>(extent.height)});
		if (std::abs(ray.direction.z) > 1e-6f) {
			const float t = -ray.origin.z / ray.direction.z;
			if (t > 0.0f && t < k_max_ground_distance) {
				point = ray.origin + ray.direction * t;
			}
		}
	}
	if (!point.has_value()) {
		return std::nullopt;
	}

	if (m_translate_snap.enabled && m_translate_snap.value > 0.0f) {
		*point = glm::round(*point / m_translate_snap.value) * m_translate_snap.value;
	}
	if (m_ruler_axis_lock) {
		*point = m_ruler.lockAxis(*point);
	}
	return point;
}

auto Workspace::rulerMouseButton(int button, int action, int mods) -> bool {
	if (button != 1) {
		return false;
	}
	m_ruler_axis_lock = (mods & k_sdl_shift) != 0;
	if (action == event::window_input_pressed) {
		m_ruler_press = m_gizmo_mouse_pos;
		return true;
	}
	if (action != event::window_input_released || !m_ruler_press.has_value()) {
		return false;
	}
	const bool click = glm::distance(*m_ruler_press, m_gizmo_mouse_pos) < k_click_distance;
	m_ruler_press.reset();
	if (click) {
		if (const std::optional<glm::vec3> point = rulerPoint()) {
			m_ruler.place(*point);
		}
		m_ruler_hover_dirty = true;
	}
	return true;
}

void Workspace::rulerKey(int key, int action) {
	if (key == k_key_left_shift || key == k_key_right_shift) {
		const bool locked = action != 0;
		if (locked != m_ruler_axis_lock) {
			m_ruler_axis_lock = locked;
			m_ruler_hover_dirty = true;
		}
		return;
	}
	if (m_gizmo_tool != GizmoTool::ruler || action != event::window_input_pressed) {
		return;
	}
	if (key == k_key_escape) {
		if (m_ruler.pending().has_value()) {
			m_ruler.cancelPending();
		} else {
			m_ruler.clear();
		}
	} else if (key == k_key_backspace || key == k_key_delete) {
		m_ruler.removeLast();
	}
	m_ruler_hover_dirty = true;
}

void Workspace::rulerTick() {
	if (!isActiveWorkspace() || m_game_camera) {
		return;
	}
	Camera* camera = renderer::getActiveCamera();
	if (camera == nullptr) {
		return;
	}
	camera->syncTransform();
	const auto extent = renderer::getOutputTarget().getExtent();
	const glm::vec2 viewport {static_cast<float>(extent.width), static_cast<float>(extent.height)};
	const bool tool_active = m_gizmo_tool == GizmoTool::ruler;

	if (tool_active) {
		const glm::vec3 forward = camera->forward();
		const bool moved = m_ruler_hover_mouse != m_gizmo_mouse_pos || m_ruler_hover_eye != camera->world_position ||
		                   m_ruler_hover_forward != forward;
		if (moved || m_ruler_hover_dirty) {
			m_ruler_hover_mouse = m_gizmo_mouse_pos;
			m_ruler_hover_eye = camera->world_position;
			m_ruler_hover_forward = forward;
			m_ruler_hover_dirty = false;
			m_ruler.setPreview(rulerPoint());
		}
	}

	m_ruler.draw(*camera, viewport, tool_active);

	event::RulerChanged snapshot = m_ruler.snapshot(*camera, viewport, tool_active);
	if (!Ruler::similar(snapshot, m_ruler_sent)) {
		snapshot.workspace_handle = m_handle.data();
		m_ruler_sent = snapshot;
		event::send<event::RulerChanged>(std::move(snapshot));
	}
}

}
