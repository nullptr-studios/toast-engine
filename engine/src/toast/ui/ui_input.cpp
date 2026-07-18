#include "ui_input.hpp"

#include <toast/log.hpp>
#include <toast/window/window_events.hpp>
#include "nodes/panels.hpp"
#include "ui_key_map.hpp"
#include "ui_system.hpp"

#include <RmlUi/Core/Context.h>
#include <ranges>

namespace ui {

namespace {

/// UI input runs before the gameplay input system so consumed events stop propagating
constexpr char k_ui_input_priority = 32;

/// Calls @p fn on every enabled panel context topmost-first until one consumes the event.
/// @p fn returns RmlUi convention: true when the event should keep propagating
template<typename F>
auto dispatchToPanels(F&& fn) -> bool {
	if (!UISystem::exists()) {
		return false;
	}

	bool consumed = false;
	for (toast::Panel* panel : std::views::reverse(UISystem::get().panels())) {
		Rml::Context* context = panel->rmlContext();
		if (!panel->enabled() || context == nullptr) {
			continue;
		}

		if (!fn(context)) {
			consumed = true;
			break;
		}
	}
	return consumed;
}

}

UIInputRouter::UIInputRouter() {
	m_listener.subscribe<event::WindowKey>(
	    [this](event::WindowKey& e) {
		    m_mods = toRmlModifiers(e.mods);
		    const Rml::Input::KeyIdentifier key = toRmlKey(e.key);
		    if (key == Rml::Input::KI_UNKNOWN) {
			    return false;
		    }

		    if (e.action == event::window_input_released) {
			    return dispatchToPanels([&](Rml::Context* context) { return context->ProcessKeyUp(key, m_mods); });
		    }
		    return dispatchToPanels([&](Rml::Context* context) { return context->ProcessKeyDown(key, m_mods); });
	    },
	    k_ui_input_priority
	);

	m_listener.subscribe<event::WindowChar>(
	    [this](event::WindowChar& e) {
		    // control characters (backspace, escape, ...) travel as key events instead
		    if (e.key < 32 || e.key == 127) {
			    return false;
		    }
		    return dispatchToPanels([&](Rml::Context* context) {
			    return context->ProcessTextInput(static_cast<Rml::Character>(e.key));
		    });
	    },
	    k_ui_input_priority
	);

	m_listener.subscribe<event::WindowMousePosition>(
	    [this](event::WindowMousePosition& e) {
		    // every panel tracks the pointer; movement is never consumed
		    dispatchToPanels([&](Rml::Context* context) {
			    context->ProcessMouseMove(static_cast<int>(e.x), static_cast<int>(e.y), m_mods);
			    return true;
		    });
		    return false;
	    },
	    k_ui_input_priority
	);

	m_listener.subscribe<event::WindowMouseButton>(
	    [this](event::WindowMouseButton& e) {
		    // SDL buttons: 1 left, 2 middle, 3 right → Rml: 0 left, 1 right, 2 middle
		    int button = 0;
		    switch (e.button) {
			    case 1: button = 0; break;
			    case 2: button = 2; break;
			    case 3: button = 1; break;
			    default: button = e.button; break;
		    }

		    if (e.action == event::window_input_pressed) {
			    return dispatchToPanels([&](Rml::Context* context) { return context->ProcessMouseButtonDown(button, m_mods); });
		    }
		    return dispatchToPanels([&](Rml::Context* context) { return context->ProcessMouseButtonUp(button, m_mods); });
	    },
	    k_ui_input_priority
	);

	m_listener.subscribe<event::WindowMouseScroll>(
	    [this](event::WindowMouseScroll& e) {
		    return dispatchToPanels([&](Rml::Context* context) {
			    return context->ProcessMouseWheel(Rml::Vector2f(-e.x, -e.y), m_mods);
		    });
	    },
	    k_ui_input_priority
	);

	m_listener.subscribe<event::WindowDisplayScale>(
	    [this](event::WindowDisplayScale& e) {
		    if (e.scale <= 0.0f) {
			    return false;
		    }
		    m_dp_ratio = e.scale;
		    if (UISystem::exists()) {
			    UISystem::get().applyDpRatio(e.scale);
		    }
		    TOAST_TRACE("UI", "Display scale changed to {}", e.scale);
		    return false;
	    },
	    k_ui_input_priority
	);
}

}
