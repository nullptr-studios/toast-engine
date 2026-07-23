#include "audio_emitter_3d.hpp"

namespace toast {

auto AudioEmitter3D::emitterPosition(const glm::vec3&) -> glm::vec3 {
	return world_position;
}

}
