/**
 * @file audio_event.hpp
 * @author Xein
 * @date 30 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "../assets.hpp"

#include <toast/export.hpp>
#include <toast/world/node.hpp>

namespace toast {

class TOAST_API [[ToastNode, Color("Beige"), Icon("AudioStreamPlayer")]] AudioEmitter : public Node {
public:
	[[Button]]
	void play();                                         ///< Fires the event

	[[Button]]
	void stop();                                         ///< Stops the event using the @c allowFadeout()

	void pause(bool value);                              ///< Pauses and resumes in a location
	void setParameter(std::string_view name, float value);    ///< Changes a parameter by name
	void setParameter(std::string_view, bool value);          ///< Changes a parameter by name
	auto isPlaying() -> bool;                            ///< @returns true if it's currently playing

	void event(std::string_view path);                   ///< Sets the event by URI
	void event(toast::UID uid);                          ///< Sets the event by UID
	void playOnEnable(bool value);                       ///< If true, will play every time the node is enabled
	void volume(float value);                            ///< @param value 0.0 - 1.0
	void pitch(float value);                             ///< @param value 0.5 - 2.0
	void allowFadeout(bool value);                       ///< False forces the event to bypass envelope settings

private:
	void onEnable();
	void onDisable();

	[[Reflect, Name("Audio Event")]]
	assets::AssetHandle<assets::AudioEvent> m_event;

	[[Reflect]]
	bool m_play_on_enable = false;

	[[Reflect, Range(0.0, 1.0)]]
	float m_volume = 1.0f;

	[[Reflect, Range(0.5, 2.0)]]
	float m_pitch = 1.0f;

	[[Reflect]]
	bool m_allow_fadeout = true;
};

}
