/**
 * @file audio_sphere_emitter.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Sphere-shaped 3D audio emitter
 */
#pragma once
#include "audio_emitter_base.hpp"

namespace toast {

/**
 * @brief Sphere-shaped audio source
 *
 * Audio comes from the sphere surface nearest to the listener, not the center. Good for
 * outdoor ambient areas and weather effects
 */
class TOAST_API [[ToastNode]] AudioSphereEmitter : public AudioEmitterBase {
public:
	void radius(float value);

protected:
	auto emitterPosition(const glm::vec3& listener) -> glm::vec3 override;

	[[Reflect, Range(0.0, 100.0)]]
	float m_radius = 2.0f;    ///< projection radius in world units
};

}
