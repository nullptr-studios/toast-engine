#include "audio_emitter_base.hpp"

#include "../audio_system.hpp"
#include "toast/time.hpp"

#include <algorithm>
#include <limits>
#include <toast/assets/asset_manager.hpp>

namespace toast {

void AudioEmitterBase::play() {
	if (!m_event.hasValue()) {
		return;
	}
	auto& sys = audio::AudioSystem::get();
	if (m_instance_id != 0) {
		sys.stopEvent3D(m_instance_id, m_allow_fadeout);
	}
	m_instance_id = sys.playEvent3D(m_event->guid());
	if (m_instance_id != 0) {
		sys.setVolume(m_instance_id, m_volume);
		sys.setPitch(m_instance_id, m_pitch);
		update3DState();
		applyProperties();
	}
}

void AudioEmitterBase::stop() {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().stopEvent3D(m_instance_id, m_allow_fadeout);
		m_instance_id = 0;
	}
}

void AudioEmitterBase::pause(bool value) const {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().pauseEvent(m_instance_id, value);
	}
}

void AudioEmitterBase::setParameter(std::string_view name, float value) const {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setParameter(m_instance_id, name, value);
	}
}

void AudioEmitterBase::setParameter(std::string_view name, bool value) const {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setParameter(m_instance_id, name, value);
	}
}

auto AudioEmitterBase::isPlaying() const -> bool {
	if (m_instance_id == 0) {
		return false;
	}
	return audio::AudioSystem::get().isEventPlaying(m_instance_id);
}

void AudioEmitterBase::event(std::string_view path) {
	auto uid_opt = assets::AssetManager::resolveURI(path);
	if (uid_opt) {
		event(*uid_opt);
	}
}

void AudioEmitterBase::event(toast::UID uid) {
	auto* asset_ptr = assets::AssetManager::get().load(uid);
	if (asset_ptr && asset_ptr->type() == "audio_event") {
		m_event = assets::AssetHandle<assets::AudioEvent>(static_cast<assets::AudioEvent*>(asset_ptr), uid);
	}
}

void AudioEmitterBase::playOnEnable(bool value) {
	m_play_on_enable = value;
}

void AudioEmitterBase::allowFadeout(bool value) {
	m_allow_fadeout = value;
}

void AudioEmitterBase::dopplerScale(float value) {
	m_doppler_scale = value;
}

void AudioEmitterBase::calculateVelocity(bool value) {
	m_calculate_velocity = value;
}

void AudioEmitterBase::volume(float value) {
	m_volume = std::clamp(value, 0.0f, 1.0f);
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setVolume(m_instance_id, m_volume);
	}
}

void AudioEmitterBase::pitch(float value) {
	m_pitch = std::clamp(value, 0.5f, 2.0f);
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setPitch(m_instance_id, m_pitch);
	}
}

void AudioEmitterBase::overrideAttenuation(bool value) {
	m_override_attenuation = value;
	applyProperties();
}

void AudioEmitterBase::minDistance(float value) {
	m_min_distance = value;
	applyProperties();
}

void AudioEmitterBase::maxDistance(float value) {
	m_max_distance = value;
	applyProperties();
}

void AudioEmitterBase::applyProperties() const {
	if (m_instance_id == 0) {
		return;
	}
	auto& sys = audio::AudioSystem::get();
	if (m_override_attenuation) {
		sys.set3DOverrideAttenuation(m_instance_id, m_min_distance, m_max_distance);
	}
}

void AudioEmitterBase::begin() {
	m_last_position = worldPos();
}

void AudioEmitterBase::lateTick() {
	if (m_instance_id != 0 && isPlaying()) {
		update3DState();
		applyProperties();
	}
}

void AudioEmitterBase::update3DState() {
	const auto& listeners = audio::AudioSystem::get().listenerPositions();

	glm::vec3 transform_pos = worldPos();
	glm::vec3 render_pos = transform_pos;
	float best_dist = std::numeric_limits<float>::max();

	if (!listeners.empty()) {
		for (const auto& listener : listeners) {
			glm::vec3 candidate = emitterPosition(listener);
			float dist = glm::distance(listener, candidate);
			if (dist < best_dist) {
				best_dist = dist;
				render_pos = candidate;
			}
		}
	}

	// Calculate velocity based ONLY on the object's actual movement in the world
	glm::vec3 vel {0.f};
	if (m_calculate_velocity && Time::delta() > 0.0f) {
		vel = (transform_pos - m_last_position) / static_cast<float>(Time::delta());
		vel *= m_doppler_scale;
	}
	m_last_position = transform_pos;

	audio::AudioSystem::get().set3DAttributes(m_instance_id, render_pos, vel, emitterForward(), emitterUp());
}

auto AudioEmitterBase::emitterPosition(const glm::vec3&) -> glm::vec3 {
	return worldPos();
}

auto AudioEmitterBase::emitterForward() -> glm::vec3 {
	return forward();
}

auto AudioEmitterBase::emitterUp() -> glm::vec3 {
	return up();
}

void AudioEmitterBase::onEnable() {
	if (m_play_on_enable) {
		play();
	}
}

void AudioEmitterBase::onDisable() {
	stop();
}

}
