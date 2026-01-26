/// @file SpineEvent.hpp
/// @author dario
/// @date 14/01/2026.

#pragma once
#include "Toast/Event/Event.hpp"

#include <utility>

struct SpineEvent : public event::Event<SpineEvent> {
	SpineEvent(const std::string_view& animationName, int track, const std::string_view& eventName, int intValue, float floatValue, const std::string_view& stringValue)
	    : eventName(std::move(eventName)),
	      animationName(std::move(animationName)),
	      track(track),
				intValue(intValue),
				floatValue(floatValue),
				stringValue(stringValue) { }

	std::string eventName;
	std::string animationName;
	int track = -1;
	
	int intValue = 0;
	float floatValue = 0.0f;
	std::string stringValue;
};
