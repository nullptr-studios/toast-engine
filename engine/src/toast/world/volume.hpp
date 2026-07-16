/**
 * @file volume.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Generic cubic volume
 */

#pragma once
#include <toast/export.hpp>
#include <toast/world/node_3d.hpp>

namespace toast {
class AudioListener;

struct VolumeTarget {
	Box<AudioListener> listener;
	glm::vec3 position;
	glm::vec3 forward;
};

class TOAST_API [[ToastNode, Hidden, Interface, Icon("Area")]] Volume : public Node3D {
public:
	/**
	 * @brief Evaluates if a target is inside the volume or not
	 * @returns true if the target is inside the volume
	 */
	virtual auto evaluateTarget(const VolumeTarget& target, float weight = 1.0f) -> bool = 0;
	virtual void resetAccumulators() = 0;

	[[nodiscard]]
	auto isGlobal() const -> bool;
	void isGlobal(bool value);

	[[nodiscard]]
	auto priority() const -> int8_t;
	void priority(int8_t value);

	[[nodiscard]]
	auto weight() const -> float;
	void weight(float value);

	[[nodiscard]]
	auto blendDistance() const -> float;
	void blendDistance(float value);

protected:
	[[nodiscard]]
	///< @brief Calculates the effect the volume should have on the object
	auto calculateWeight(const VolumeTarget& target) -> float;

	/**
	 * @brief Calculates the point on the bounds of the volume that is closest to the given point
	 * @returns parameter @c point if the point is inside the volume
	 */
	[[nodiscard]]
	auto closestPointOnBounds(glm::vec3 point) -> glm::vec3;

private:
	[[Reflect]]
	bool m_is_global = false;

	[[Reflect]]
	int8_t m_priority = 0;

	[[Reflect, Range(0.0, 1.0)]]
	float m_weight = 1.0f;

	[[Reflect, Unit("m")]]
	float m_blend_distance = 0.0f;
};

}
