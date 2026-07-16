/**
 * @file audio_emitter_3d.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Point-source 3D emitter, positioned at the node's world origin
 */
#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

/**
 * @brief Simple point-source 3D emitter
 *
 * Audio emanates from the node's world position. No shape projection, just direct positioning
 */
class TOAST_API [[ToastNode]] AudioEmitter3D : public AudioEmitterBase {
protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;
};

}
