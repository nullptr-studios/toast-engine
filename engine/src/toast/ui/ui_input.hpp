/**
 * @file ui_input.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Routes window input events to the UI panels
 */

#pragma once
#include <toast/events/listener.hpp>
#include <unordered_set>

namespace ui {

class UIInputRouter {
public:
	UIInputRouter();

	[[nodiscard]]
	auto dpRatio() const -> float {
		return m_dp_ratio;
	}

private:
	event::Listener m_listener;

	/// Presses the UI took so only their releases are consumed and gameplay never misses a release
	std::unordered_set<int> m_ui_buttons;
	std::unordered_set<int> m_ui_keys;

	void updateCursorLock();

	float m_dp_ratio = 1.0f;
	int m_mods = 0;
	bool m_overlay_mouse = false;
	bool m_game_mouse_locked = false;
	bool m_overlay_cursor = false;
	/// A locked cursor is a virtual point that wanders with the look so panels must not hit test it
	bool m_cursor_locked = false;
};

}
