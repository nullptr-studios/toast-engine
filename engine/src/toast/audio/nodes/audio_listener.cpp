#include "audio_listener.hpp"

#include "../audio_system.hpp"

#include <toast/time.hpp>

namespace toast {

void AudioListener::index(int value) {
	TOAST_INFO("Audio", "Index from Listener in {} changed from {} to {}", box(), value, m_index);
	m_index = value;
}

void AudioListener::weight(float value) {
	m_weight = value;
	m_weight_changed = true;
}

void AudioListener::begin() {
	prev_position = worldPos();
	prev_rotation = worldRot();
	audio::AudioSystem::get().registerListener(box().as<AudioListener>());
}

void AudioListener::end() {
	audio::AudioSystem::get().unregisterListener(box().as<AudioListener>());
}

void toast::AudioListener::lateTick() {
	// Update listener position
	audio::AudioSystem::get().setListenerPosition(m_index, worldPos());

	// Calculate velocity for doppler
	glm::vec3 pos = worldPos();
	glm::vec3 rot = worldRot();
	glm::vec3 velocity = (pos - prev_position) / static_cast<float>(Time::delta());

	if (m_weight_changed) {
		audio::AudioSystem::get().updateListenerWeight(m_index, m_weight);
	}

	if (pos == prev_position && rot == prev_rotation) {
		return;    // no need to update the fmod position
	}

	glm::vec3 forward_vec = forward();
	glm::vec3 up_vec = up();
	std::optional<glm::vec3> attenuation =
	    attenuation_override.exists() ? std::optional {attenuation_override->pos()} : std::nullopt;
	audio::AudioSystem::get().updateListenerAttributes(m_index, pos, velocity, forward_vec, up_vec, attenuation);
}

}
