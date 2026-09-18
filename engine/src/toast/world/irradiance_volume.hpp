/**
 * @file irradiance_volume.hpp
 * @author dario
 * @date 07/08/2026
 *
 * @brief A grid of baked irradiance probes; the diffuse half of global illumination
 */

#pragma once
#include "node_3d.hpp"

#include <algorithm>
#include <toast/export.hpp>

namespace toast {

/**
 * @brief A box filled with a regular grid of probes storing the light arriving at each point
 *
 * Image-based lighting lights a surface from an environment that is infinitely distant by construction, so
 * it has no idea what is standing next to what. A red wall never tints the floor beside it, a doorway throws
 * no light into the room behind it, and an interior lit through one window is uniformly flat. Reflection
 * probes improve the specular half of that and carry a room's own radiance, but only where an artist placed
 * one and only as a single point of view
 *
 * A volume covers a whole space instead. Each probe stores the irradiance arriving at its position as
 * spherical harmonics - four coefficients per colour channel, which is enough for diffuse - and a surface
 * inside the volume interpolates the eight probes around it. That is what makes indirect light vary across a
 * room rather than being one flat ambient value
 *
 * @note Baked, not dynamic. Nothing here responds to a light moving or a door opening; that is DDGI, and it
 *       needs ray tracing to be practical. The storage and the shading lookup are deliberately identical
 *       either way, so swapping the bake for a live update later touches only the bake
 */
class [[ToastNode]] TOAST_API IrradianceVolume : public Node3D {
public:
	IrradianceVolume() = default;
	~IrradianceVolume() override = default;

	/// @returns half-extents of the box the probe grid fills
	[[nodiscard]]
	auto extents() const noexcept -> const glm::vec3& {
		return m_extents;
	}

	/// @returns distance between neighbouring probes, in world units
	[[nodiscard]]
	auto spacing() const noexcept -> float {
		return std::max(m_spacing, 0.1f);
	}

	[[nodiscard]]
	auto intensity() const noexcept -> float {
		return m_intensity;
	}

	/// @returns probe count along each axis, at least 2 so trilinear interpolation always has a cell
	[[nodiscard]]
	auto probeCounts() const noexcept -> glm::uvec3 {
		const glm::vec3 size = m_extents * 2.0f;
		const float step = spacing();
		return {
		  static_cast<uint32_t>(std::clamp(static_cast<int>(size.x / step) + 1, 2, 32)),
		  static_cast<uint32_t>(std::clamp(static_cast<int>(size.y / step) + 1, 2, 32)),
		  static_cast<uint32_t>(std::clamp(static_cast<int>(size.z / step) + 1, 2, 32))
		};
	}

	/// @returns how many probes this volume bakes
	[[nodiscard]]
	auto probeCount() const noexcept -> uint32_t {
		const glm::uvec3 counts = probeCounts();
		return counts.x * counts.y * counts.z;
	}

private:
	void init();
	void end();
	void destroy();

	/// Half-extents of the volume. Probes fill it edge to edge, so this wants to cover the space that needs
	/// indirect light rather than the geometry inside it
	[[Reflect, Name("Extents"), Unit("m")]] alignas(16) glm::vec3 m_extents = glm::vec3(10.0f);

	/// Distance between probes. Cost is cubic in this: halving it multiplies the bake by eight, and each probe
	/// is six rendered frames. Indirect diffuse is low-frequency, so a coarse grid is usually enough - go
	/// finer only where light changes sharply across a short distance, like a doorway
	[[Reflect, Name("Probe Spacing"), Unit("m"), Range(0.1, 50.0)]]
	float m_spacing = 4.0f;

	[[Reflect, Name("Intensity"), Range(0.0, 4.0)]]
	float m_intensity = 1.0f;

	bool m_registered_proxy = false;
};

}
