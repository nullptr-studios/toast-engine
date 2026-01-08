/// @file SimulateWorldEvent.hpp
/// @date 6/11/2025
/// @author Xein

#pragma once
#ifdef TOAST_EDITOR
#include "Toast/Event/Event.hpp"
#include "Toast/Log.hpp"

namespace toast {

// This event should be used for the play pause button on the editor
struct SimulateWorldEvent final : public event::Event<SimulateWorldEvent> {
	SimulateWorldEvent(bool value) : value(value) {
		// TOAST_INFO("World simulation: {0}", value ? "On" : "Off");
	}

	bool value = false;
};

}

#endif
