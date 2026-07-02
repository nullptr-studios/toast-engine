#pragma once
#include <toast/events/event.hpp>
#include <toast/export.hpp>

#include <string>

namespace event {

// TODO: refactor when we have the message system

struct TOAST_API MusicBeat : Event<MusicBeat> {
	int   bar;
	int   beat;
	int   position_ms;
	float tempo;
	int   time_sig_upper;
	int   time_sig_lower;

	MusicBeat(int bar, int beat, int position_ms, float tempo, int upper, int lower)
	    : bar(bar), beat(beat), position_ms(position_ms), tempo(tempo), time_sig_upper(upper), time_sig_lower(lower) { }
};

struct TOAST_API MusicMarker : Event<MusicMarker> {
	std::string name;
	int         position_ms;

	MusicMarker(std::string name, int position_ms) : name(std::move(name)), position_ms(position_ms) { }
};

struct TOAST_API MusicStopped : Event<MusicStopped> {
	// TODO: refactor when we have the message system
};

}
