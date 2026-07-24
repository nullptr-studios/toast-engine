#include "audio_capsule_emitter.hpp"

namespace toast {

void AudioCapsuleEmitter::radius(float value) {
	m_radius = value;
}

void AudioCapsuleEmitter::halfHeight(float value) {
	m_half_height = value;
}

auto AudioCapsuleEmitter::emitterPosition(const glm::vec3& listener) -> glm::vec3 {
	glm::vec3 center = world_position;
	glm::vec3 axis = forward();
	glm::vec3 a = center - axis * m_half_height;
	glm::vec3 b = center + axis * m_half_height;

	glm::vec3 ab = b - a;
	float ab_len_sq = glm::dot(ab, ab);
	if (ab_len_sq == 0.0f) {
		return center;
	}

	float t = glm::clamp(glm::dot(listener - a, ab) / ab_len_sq, 0.0f, 1.0f);
	glm::vec3 closest_on_segment = a + t * ab;

	glm::vec3 diff = listener - closest_on_segment;
	float dist = glm::length(diff);
	if (dist <= m_radius) {
		return listener;
	}
	return closest_on_segment + (diff / dist) * m_radius;
}

}
