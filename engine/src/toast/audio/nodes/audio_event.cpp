#include "audio_event.hpp"

#include "toast/audio/audio_system.hpp"

#include <toast/assets/assets.hpp>

namespace toast {

void AudioEvent::play() {
	if (!m_event.hasValue()) return;

	auto& sys = audio::AudioSystem::get();
	std::string_view guid_str = m_event->guid();

	sys.playEvent(guid_str);
	sys.setVolume(guid_str, m_volume);
	sys.setPitch(guid_str, m_pitch);
}

void AudioEvent::stop() {
	if (!m_event.hasValue()) return;
	audio::AudioSystem::get().stopEvent(m_event->guid(), m_allow_fadeout);
}

void AudioEvent::pause(bool value) {
	if (!m_event.hasValue()) return;
	audio::AudioSystem::get().pauseEvent(m_event->guid(), value);
}

void AudioEvent::setParameter(std::string name, float value) {
	if (!m_event.hasValue()) return;
	audio::AudioSystem::get().setParameter(m_event->guid(), name, value);
}

void AudioEvent::setParameter(std::string name, bool value) {
	if (!m_event.hasValue()) return;
	audio::AudioSystem::get().setParameter(m_event->guid(), name, value);
}

auto AudioEvent::isPlaying() -> bool {
	if (!m_event.hasValue()) return false;
	return audio::AudioSystem::get().isEventPlaying(m_event->guid());
}

void AudioEvent::event(std::string_view path) {
	m_event = assets::load<assets::AudioEvent>(path);
}

void AudioEvent::event(toast::UID uid) {
	m_event = assets::load<assets::AudioEvent>(uid);
}

void AudioEvent::playOnEnable(bool value) {
	m_play_on_enable = value;
}
void AudioEvent::volume(float value) {
	m_volume = std::clamp(value, 0.0f, 1.0f);
	if (m_event.hasValue()) {
		audio::AudioSystem::get().setVolume(m_event->guid(), m_volume);
	}
}
void AudioEvent::pitch(float value) {
	m_pitch = std::clamp(value, 0.5f, 2.0f);
	if (m_event.hasValue()) {
		audio::AudioSystem::get().setPitch(m_event->guid(), m_pitch);
	}
}
void AudioEvent::allowFadeout(bool value) {
	m_allow_fadeout = value;
}

void AudioEvent::onEnable() {
	if (m_play_on_enable) {
		play();
	}
}

void AudioEvent::onDisable() {
	stop();
}

}