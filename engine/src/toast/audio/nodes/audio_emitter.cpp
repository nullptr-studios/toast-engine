#include "audio_emitter.hpp"

#include <toast/assets/assets.hpp>
#include <toast/audio/audio_event.hpp>
#include <toast/audio/audio_system.hpp>

namespace toast {

void AudioEmitter::play() {
	if (!m_event.hasValue()) {
		return;
	}

	auto& sys = audio::AudioSystem::get();
	std::string_view guid_str = m_event->guid();

	sys.playEvent(guid_str);
	sys.setVolume(guid_str, m_volume);
	sys.setPitch(guid_str, m_pitch);
}

void AudioEmitter::stop() {
	if (!m_event.hasValue()) {
		return;
	}
	audio::AudioSystem::get().stopEvent(m_event->guid(), m_allow_fadeout);
}

void AudioEmitter::pause(bool value) {
	if (!m_event.hasValue()) {
		return;
	}
	audio::AudioSystem::get().pauseEvent(m_event->guid(), value);
}

void AudioEmitter::setParameter(std::string_view name, float value) {
	if (!m_event.hasValue()) {
		return;
	}
	audio::AudioSystem::get().setParameter(m_event->guid(), name, value);
}

void AudioEmitter::setParameter(std::string_view name, bool value) {
	if (!m_event.hasValue()) {
		return;
	}
	audio::AudioSystem::get().setParameter(m_event->guid(), name, value);
}

auto AudioEmitter::isPlaying() -> bool {
	if (!m_event.hasValue()) {
		return false;
	}
	return audio::AudioSystem::get().isEventPlaying(m_event->guid());
}

void AudioEmitter::event(std::string_view path) {
	m_event = assets::load<assets::AudioEvent>(path);
}

void AudioEmitter::event(toast::UID uid) {
	m_event = assets::load<assets::AudioEvent>(uid);
}

void AudioEmitter::playOnEnable(bool value) {
	m_play_on_enable = value;
}

void AudioEmitter::volume(float value) {
	m_volume = std::clamp(value, 0.0f, 1.0f);
	if (m_event.hasValue()) {
		audio::AudioSystem::get().setVolume(m_event->guid(), m_volume);
	}
}

void AudioEmitter::pitch(float value) {
	m_pitch = std::clamp(value, 0.5f, 2.0f);
	if (m_event.hasValue()) {
		audio::AudioSystem::get().setPitch(m_event->guid(), m_pitch);
	}
}

void AudioEmitter::allowFadeout(bool value) {
	m_allow_fadeout = value;
}

void AudioEmitter::onEnable() {
	if (m_play_on_enable) {
		play();
	}
}

void AudioEmitter::onDisable() {
	stop();
}

}
