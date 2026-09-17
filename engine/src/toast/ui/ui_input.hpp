/**
 * @file ui_input.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Routes window input events to the UI panels
 */

#pragma once
#include <toast/events/listener.hpp>

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

	float m_dp_ratio = 1.0f;
	int m_mods = 0;
};

}
