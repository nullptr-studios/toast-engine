#include "audio_event_3d.hpp"

#include "../audio_system.hpp"
#include "toast/time.hpp"

#include <algorithm>
#include <toast/assets/asset_manager.hpp>

namespace toast {

void AudioEvent3D::play() {
	if (!m_event.hasValue()) return;

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

void AudioEvent3D::stop() {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().stopEvent3D(m_instance_id, m_allow_fadeout);
		m_instance_id = 0;
	}
}

void AudioEvent3D::pause(bool value) {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().pauseEvent(m_instance_id, value);
	}
}

void AudioEvent3D::setParameter(std::string_view name, float value) {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setParameter(m_instance_id, name, value);
	}
}

void AudioEvent3D::setParameter(std::string_view name, bool value) {
	if (m_instance_id != 0) {
		audio::AudioSystem::get().setParameter(m_instance_id, name, value);
	}
}

auto AudioEvent3D::isPlaying() -> bool {
	if (m_instance_id == 0) return false;
	return audio::AudioSystem::get().isEventPlaying(m_instance_id); 
}

void AudioEvent3D::event(std::string_view path) {
	auto uid_opt = assets::AssetManager::resolveURI(path);
	if (uid_opt) {
		event(*uid_opt);
	}
}

void AudioEvent3D::event(toast::UID uid) {
	auto* asset_ptr = assets::AssetManager::get().load(uid);
	if (asset_ptr && asset_ptr->type() == "audio_event") {
		m_event = assets::AssetHandle<assets::AudioEvent>(static_cast<assets::AudioEvent*>(asset_ptr), uid);
	}
}

void AudioEvent3D::playOnEnable(bool value) { m_play_on_enable = value; }
void AudioEvent3D::allowFadeout(bool value) { m_allow_fadeout = value; }
void AudioEvent3D::dopplerScale(float value) { m_doppler_scale = value; }
void AudioEvent3D::calculateVelocity(bool value) { m_calculate_velocity = value; }

void AudioEvent3D::volume(float value) {
	m_volume = std::clamp(value, 0.0f, 1.0f);
	if (m_instance_id != 0) audio::AudioSystem::get().setVolume(m_instance_id, m_volume);
}

void AudioEvent3D::pitch(float value) {
	m_pitch = std::clamp(value, 0.5f, 2.0f);
	if (m_instance_id != 0) audio::AudioSystem::get().setPitch(m_instance_id, m_pitch);
}

void AudioEvent3D::overrideAttenuation(bool value) {
	m_override_attenuation = value;
	applyProperties();
}

void AudioEvent3D::minDistance(float value) {
	m_min_distance = value;
	applyProperties();
}

void AudioEvent3D::maxDistance(float value) {
	m_max_distance = value;
	applyProperties();
}

void AudioEvent3D::applyProperties() {
	if (m_instance_id == 0) return;
    
	auto& sys = audio::AudioSystem::get();
    
	if (m_override_attenuation) {
		sys.set3DOverrideAttenuation(m_instance_id, m_min_distance, m_max_distance);
	}
}

void AudioEvent3D::begin() {
	m_last_position = worldPos();
}

void AudioEvent3D::lateTick() {
	if (m_instance_id != 0 && isPlaying()) {
		update3DState();
		applyProperties();
	}
}

void AudioEvent3D::update3DState() {
	glm::vec3 pos = worldPos(); 
	glm::vec3 fwd = forward();
	glm::vec3 up_v  = up();
	glm::vec3 vel{0.f};

	if (m_calculate_velocity && Time::delta() > 0.0f) {
		vel = (pos - m_last_position) / static_cast<float>(Time::delta());
	}
	
	vel *= m_doppler_scale;
	m_last_position = pos;

	audio::AudioSystem::get().set3DAttributes(m_instance_id, pos, vel, fwd, up_v);
}

void AudioEvent3D::onEnable() {
	if (m_play_on_enable) play();
}

void AudioEvent3D::onDisable() {
	stop();
}

}
