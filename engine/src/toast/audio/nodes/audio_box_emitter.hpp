/**
 * @file audio_box_emitter.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Box-shaped 3D audio emitter
 */
#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

/**
 * @brief Box-shaped audio source
 *
 * Audio appears to come from the nearest point inside the box to the listener, not the center.
 * Useful for large ambient areas like rooms or caverns
 */
class TOAST_API [[ToastNode]] AudioBoxEmitter : public AudioEmitterBase {
public:
	void extents(glm::vec3 value);

protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;

	[[Reflect]]
	glm::vec3 m_extents = glm::vec3 {1.0f};    ///< half-extents in local space
};

}
