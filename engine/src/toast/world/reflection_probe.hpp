/**
 * @file reflection_probe.hpp
 * @author dario
 * @date 05/08/2026
 *
 * @brief Localised reflection volume; registers with the renderer so it can be collected per frame
 */

#pragma once
#include "node_3d.hpp"

#include <algorithm>
#include <toast/export.hpp>

namespace toast {

/**
 * @brief A volume in which reflections are reprojected onto local geometry instead of infinity
 *
 * An environment cubemap is infinitely distant by construction: every surface in the world reflects it as
 * though the room were unbounded, so a wall two metres away and the sky behind it arrive from the same
 * direction. Indoors that reads as wrong immediately - reflections slide across surfaces as the camera moves
 * rather than staying attached to the geometry that should be producing them
 *
 * A probe fixes that by describing the shape of the space it covers. The reflection ray is intersected
 * against that shape and re-aimed at the hit point, so a reflection lands on the wall it belongs to. Where
 * probes do not reach, shading falls back to the global environment, which is why the whole thing degrades
 * gracefully rather than popping
 *
 * @note A probe reprojects its own captured cubemap once baked, and the global environment until then - so an
 *       unbaked probe still corrects the *shape* of reflections, just not their content
 */
class [[ToastNode, Icon("Eclipse")]] TOAST_API ReflectionProbe : public Node3D {
public:
	ReflectionProbe() = default;
	~ReflectionProbe() override = default;

	/// @returns radius at which this probe stops contributing entirely
	[[nodiscard]]
	auto influenceRadius() const noexcept -> float {
		return m_influence_radius;
	}

	/// @returns half-extents of the box the reflection ray is intersected against
	[[nodiscard]]
	auto boxExtents() const noexcept -> const glm::vec3& {
		return m_box_extents;
	}

	[[nodiscard]]
	auto intensity() const noexcept -> float {
		return m_intensity;
	}

	[[nodiscard]]
	auto usesBoxProjection() const noexcept -> bool {
		return m_box_projection;
	}

	/// @returns face resolution of this probe's cubemap, clamped to what the pass supports
	[[nodiscard]]
	auto resolution() const noexcept -> uint32_t {
		return static_cast<uint32_t>(std::clamp(m_resolution, 32, 512));
	}

	/// @returns true when this probe loads its capture from disk rather than baking it each run
	[[nodiscard]]
	auto isPrebaked() const noexcept -> bool {
		return m_prebaked;
	}

private:
	void init();
	void end();
	void destroy();

	/// Distance past which the probe contributes nothing. Inside it, contribution ramps up smoothly - a hard
	/// cutoff would make reflections pop as the camera crosses the boundary
	///
	/// **Sphere projection only.** With Box Projection on, the box *is* the influence volume: a projection that
	/// re-aims rays at the inside of a box is meaningless for a surface outside it, so authority has to end
	/// where the box does
	[[Reflect, Name("Influence Radius"), Unit("m"), Range(0.1, 500.0)]]
	float m_influence_radius = 10.0f;

	/// Half-extents of the projection volume, centred on the probe. Set these to the room the probe sits in:
	/// they are the surfaces the reflection is re-aimed at, so they want to match the walls, not the contents
	///
	/// This doubles as the probe's influence volume, so a box that does not enclose the geometry it is meant to
	/// correct does nothing at all. Mind the scene's units - the default is sized for metres, and a scene
	/// authored in centimetres needs extents a hundred times larger. The volume is drawn in the viewport
	[[Reflect, Name("Box Extents"), Unit("m"), Group("Projection")]] alignas(16) glm::vec3 m_box_extents = glm::vec3(5.0f);

	/// Box projection suits rooms and corridors. Turning it off falls back to treating the probe as a sphere
	/// of the influence radius, which is the better fit for open or rounded spaces
	[[Reflect, Name("Box Projection"), Group("Projection")]]
	bool m_box_projection = true;

	[[Reflect, Name("Intensity"), Range(0.0, 8.0)]]
	float m_intensity = 1.0f;

	/// Face resolution of this probe's cubemap. Costs memory as the square: a 512 probe carries sixteen times
	/// the texels of a 128 one, and the whole chain is resident for as long as the probe exists. Worth raising
	/// for a probe a polished mirror sits in, and worth leaving low for one covering a rough stone interior,
	/// where the roughness chain throws the detail away regardless
	[[Reflect, Name("Resolution"), Range(32.0, 512.0), Group("Capture")]]
	int m_resolution = 128;

	/// Baking every launch costs six frames per probe and re-does work that has not changed. Prebaked stores
	/// the capture next to the project and loads it at startup instead, which is what a shipped game wants -
	/// nothing in a build should be spending frames rediscovering a static room
	[[Reflect, Name("Prebaked"), Group("Capture")]]
	bool m_prebaked = false;

	bool m_registered_proxy = false;
};

}
