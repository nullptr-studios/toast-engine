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
	prev_position = world_position;
	prev_rotation = glm::eulerAngles(world_rotation);
	audio::AudioSystem::get().registerListener(box().as<AudioListener>());
}

void AudioListener::end() {
	audio::AudioSystem::get().unregisterListener(box().as<AudioListener>());
}

void toast::AudioListener::lateTick() {
	// Update listener position
	audio::AudioSystem::get().setListenerPosition(m_index, world_position);

	// Calculate velocity for doppler
	glm::vec3 pos = world_position;
	glm::vec3 rot = glm::eulerAngles(world_rotation);
	glm::vec3 velocity {0.0f};
	if (Time::delta() > 0.0f) {
		velocity = (pos - prev_position) / static_cast<float>(Time::delta());
	}

	if (m_weight_changed) {
		audio::AudioSystem::get().updateListenerWeight(m_index, m_weight);
		m_weight_changed = false;
	}

	if (pos == prev_position && rot == prev_rotation) {
		return;    // no need to update the fmod position
	}

	glm::vec3 forward_vec = forward();
	glm::vec3 up_vec = up();
	std::optional<glm::vec3> attenuation =
	    attenuation_override.exists() ? std::optional {attenuation_override->position} : std::nullopt;
	audio::AudioSystem::get().updateListenerAttributes(m_index, pos, velocity, forward_vec, up_vec, attenuation);

	prev_position = pos;
	prev_rotation = rot;
}

}
