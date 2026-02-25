/// @file SowUD.h
/// @author dario
/// @date 25/02/2026.

#pragma once

#include <Toast/Event/Event.hpp>

class ShowHUDLayerEvent : public event::Event<ShowHUDLayerEvent> {
public:
	ShowHUDLayerEvent(const bool s) : s(s) { }

	bool s = false;
};
