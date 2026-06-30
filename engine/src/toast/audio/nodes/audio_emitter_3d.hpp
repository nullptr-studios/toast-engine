#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

class TOAST_API [[ToastNode]] AudioEmitter3D : public AudioEmitterBase {
protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;
};

}
