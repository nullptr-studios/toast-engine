/**
 * @file volume.hpp
 * @author Xein
 * @date 01 Jul 2026
 *
 * @brief Generic cubic volume
 */

#pragma once
#include "toast/events/signals.hpp"

#include <toast/export.hpp>
#include <toast/world/node_3d.hpp>

namespace toast {
class AudioListener;

/// @brief What a volume is being evaluated against: a listener, a camera, anything with a position
struct VolumeTarget {
	/// Audio only, and the identity AudioVolume tracks enter/exit by. Empty for every other kind of target
	Box<AudioListener> listener;

	glm::vec3 position;
	glm::vec3 forward;
};

class TOAST_API [[ToastNode, Hidden, Interface, Icon("Area")]] Volume : public Node3D {
public:
	[[Reflect, Color]]
	glm::vec4 debug_color = glm::vec4(0.0f, 1.0f, 0.251f, 0.5f);
	[[Reflect]]
	bool debug_fill = true;
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

	signals::Signal<Box<Node>> node_exited;     // TODO:
	signals::Signal<Box<Node>> node_entered;    // TODO:

protected:
	void updateInspectorMessages() override;

	[[nodiscard]]
	///< @brief Calculates the effect the volume should have on the object
	auto calculateWeight(const VolumeTarget& target) const -> float;

	/**
	 * @brief Calculates the point on the bounds of the volume that is closest to the given point
	 * @returns parameter @c point if the point is inside the volume
	 *
	 * The box is the node's transform applied to a unit cube, so scaling the node sizes it. Virtual because a
	 * volume that carries explicit half-extents instead needs its own box, and calculateWeight() is the only
	 * caller either way
	 */
	[[nodiscard]]
	virtual auto closestPointOnBounds(glm::vec3 point) const -> glm::vec3;

private:
	void init();
	void destroy();
	void onEnable();
	void onDisable();
	void drawDebug();
	bool m_debug_visible = false;
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
