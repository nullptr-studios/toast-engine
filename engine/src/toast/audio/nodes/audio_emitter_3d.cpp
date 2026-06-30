#include "audio_emitter_3d.hpp"

namespace toast {

glm::vec3 AudioEmitter3D::emitterPosition(const glm::vec3&) {
	return worldPos();
}

}
