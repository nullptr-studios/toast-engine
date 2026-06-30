#include "audio_capsule_emitter.hpp"

namespace toast {

void AudioCapsuleEmitter::radius(float value) {
	m_radius = value;
}

void AudioCapsuleEmitter::halfHeight(float value) {
	m_half_height = value;
}

glm::vec3 AudioCapsuleEmitter::emitterPosition(const glm::vec3& listener) {
	glm::vec3 center = worldPos();
	glm::vec3 axis = forward();
	glm::vec3 a = center - axis * m_half_height;
	glm::vec3 b = center + axis * m_half_height;

	glm::vec3 ab = b - a;
	float abLenSq = glm::dot(ab, ab);
	if (abLenSq == 0.0f) return center;

	float t = glm::clamp(glm::dot(listener - a, ab) / abLenSq, 0.0f, 1.0f);
	glm::vec3 closestOnSegment = a + t * ab;

	glm::vec3 diff = listener - closestOnSegment;
	float dist = glm::length(diff);
	if (dist <= m_radius) return listener;
	return closestOnSegment + (diff / dist) * m_radius;
}

}
