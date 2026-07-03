/**
 * @file audio_events.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Engine events fired by MusicPlayer on beat, marker, and stop
 */
#pragma once
#include <string>
#include <toast/events/event.hpp>
#include <toast/export.hpp>

namespace event {

// TODO: refactor when we have the message system

/**
 * @brief Fired when MusicPlayer reaches a beat or bar boundary
 */
struct TOAST_API MusicBeat : Event<MusicBeat> {
	int bar;               ///< 0-based bar number
	int beat;              ///< 0-based beat within the bar
	int position_ms;
	float tempo;
	int time_sig_upper;    ///< numerator of the time signature
	int time_sig_lower;    ///< denominator of the time signature

	MusicBeat(int bar, int beat, int position_ms, float tempo, int upper, int lower)
	    : bar(bar),
	      beat(beat),
	      position_ms(position_ms),
	      tempo(tempo),
	      time_sig_upper(upper),
	      time_sig_lower(lower) { }
};

/**
 * @brief Fired when MusicPlayer reaches a named marker in the timeline
 */
struct TOAST_API MusicMarker : Event<MusicMarker> {
	std::string name;
	int position_ms;

	MusicMarker(std::string name, int position_ms) : name(std::move(name)), position_ms(position_ms) { }
};

/**
 * @brief Fired when a track finishes playing
 */
struct TOAST_API MusicStopped : Event<MusicStopped> {
	// TODO: refactor when we have the message system
};

}
