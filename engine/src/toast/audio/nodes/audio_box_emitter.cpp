#include "audio_box_emitter.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace toast {

void AudioBoxEmitter::extents(glm::vec3 value) {
	m_extents = value;
}

auto AudioBoxEmitter::emitterPosition(const glm::vec3& listener) -> glm::vec3 {
	glm::mat4 world = getWorldTransform();
	glm::mat4 inv_world = glm::inverse(world);
	glm::vec3 local_listener = glm::vec3(inv_world * glm::vec4(listener, 1.0f));
	glm::vec3 half = m_extents * 0.5f;
	glm::vec3 clamped = glm::clamp(local_listener, -half, half);
	return {world * glm::vec4(clamped, 1.0f)};
}

}
