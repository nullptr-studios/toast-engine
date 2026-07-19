#include "audio_sphere_emitter.hpp"

namespace toast {

void AudioSphereEmitter::radius(float value) {
	m_radius = value;
}

auto AudioSphereEmitter::emitterPosition(const glm::vec3& listener) -> glm::vec3 {
	glm::vec3 center = world_position;
	glm::vec3 diff = listener - center;
	float dist = glm::length(diff);
	if (dist <= m_radius) {
		return listener;
	}
	return center + (diff / dist) * m_radius;
}

}
