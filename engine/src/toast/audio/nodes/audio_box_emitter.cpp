#include "audio_box_emitter.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace toast {

void AudioBoxEmitter::extents(glm::vec3 value) {
	m_extents = value;
}

glm::vec3 AudioBoxEmitter::emitterPosition(const glm::vec3& listener) {
	glm::mat4 world = getWorldTransform();
	glm::mat4 invWorld = glm::inverse(world);
	glm::vec3 localListener = glm::vec3(invWorld * glm::vec4(listener, 1.0f));
	glm::vec3 half = m_extents * 0.5f;
	glm::vec3 clamped = glm::clamp(localListener, -half, half);
	return glm::vec3(world * glm::vec4(clamped, 1.0f));
}

}
