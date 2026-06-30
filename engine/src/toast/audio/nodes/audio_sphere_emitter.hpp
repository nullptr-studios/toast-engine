#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

class TOAST_API [[ToastNode]] AudioSphereEmitter : public AudioEmitterBase {
public:
	void radius(float value);

protected:
	glm::vec3 emitterPosition(const glm::vec3& listener) override;

	[[Reflect, Range(0.0, 100.0)]]
	float m_radius = 2.0f;
};

}
