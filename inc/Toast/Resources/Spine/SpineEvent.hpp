/// @file SpineEvent.hpp
/// @author dario
/// @date 14/01/2026.

#pragma once
#include "Toast/Event/Event.hpp"

#include <utility>

struct SpineEvent : public event::Event<SpineEvent> {
	SpineEvent(
	    unsigned int uniqueID, std::string_view animationName, int track, std::string_view eventName, int intValue, float floatValue,
	    const std::string_view& stringValue
	)
	    : uniqueID(uniqueID),
	      eventName(eventName),
	      animationName(animationName),
	      track(track),
	      intValue(intValue),
	      floatValue(floatValue),
	      stringValue(stringValue) { }

	unsigned int uniqueID = 0;

	std::string eventName;
	std::string animationName;
	int track = -1;

	int intValue = 0;
	float floatValue = 0.0f;
	std::string stringValue;
};

struct SpineAnimationPlaybackEvent : public event::Event<SpineAnimationPlaybackEvent> {
	
	enum class Type {
		Start,
		Interrupt,
		End,
		Complete,
		Dispose
	};
	
	SpineAnimationPlaybackEvent(unsigned int uniqueID, std::string_view animationName, int track, Type type)
	    : uniqueID(uniqueID),
	      animationName(animationName),
	      track(track),
	      playbackType(type) { }

	unsigned int uniqueID = 0;
	std::string animationName;
	int track = -1;
	Type playbackType;
};
