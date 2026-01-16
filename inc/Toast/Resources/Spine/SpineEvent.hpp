/// @file SpineEvent.hpp
/// @author dario
/// @date 14/01/2026.

#pragma once
#include <utility>

#include "Toast/Event/Event.hpp"


struct SpineEvent : public event::Event<SpineEvent> {
	SpineEvent(const std::string_view& animationName, int track, const std::string_view& eventName) : eventName(std::move(eventName)), animationName(std::move(animationName)), track(track)  {}

	std::string eventName;
	std::string animationName;
	int track = -1;
};