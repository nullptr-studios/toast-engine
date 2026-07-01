#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

class TOAST_API [[ToastNode]] AudioBoxEmitter : public AudioEmitterBase {
public:
	void extents(glm::vec3 value);

protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;

	[[Reflect]]
	glm::vec3 m_extents = glm::vec3 {1.0f};
};

}
