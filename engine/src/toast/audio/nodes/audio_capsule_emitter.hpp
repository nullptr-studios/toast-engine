/**
 * @file audio_capsule_emitter.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Capsule-shaped 3D audio emitter
 */
#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

/**
 * @brief Capsule-shaped audio source
 *
 * Like a sphere stretched along Y, audio comes from the capsule surface. Useful for corridors,
 * tunnels, and tall vertical spaces
 */
class TOAST_API [[ToastNode]] AudioCapsuleEmitter : public AudioEmitterBase {
public:
	void radius(float value);
	void halfHeight(float value);

protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;

	[[Reflect, Range(0.0, 100.0)]]
	float m_radius = 0.5f;         ///< radius in world units

	[[Reflect, Range(0.0, 100.0)]]
	float m_half_height = 1.0f;    ///< half the capsule length along Y
};

}
