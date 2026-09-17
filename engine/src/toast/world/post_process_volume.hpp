/**
 * @file post_process_volume.hpp
 * @author dario
 * @date 19/08/2026
 *
 * @brief Per-level post-process grading, as a node an artist places in the scene
 */

#pragma once
#include "volume.hpp"

#include <toast/export.hpp>
#include <toast/renderer/post_process_settings.hpp>

namespace toast {

/**
 * @brief A region of the world that grades the post chain while the camera is inside it
 *
 * Post-process values are scene content, not machine settings. The right exposure in a cave is the wrong one
 * on a hilltop, and neither is a property of the hardware - so they live here, per level, rather than in the
 * settings registry beside shadow resolution and frame rate
 *
 * A volume is either **global**, applying everywhere as the level's base grade, or a **box** that takes over
 * as the camera enters it. Several may overlap: the renderer sorts them by priority and blends each one in
 * turn, so a level typically has one global volume for its overall look and small boxes for the rooms that
 * differ from it
 *
 * @note The blend is a crossfade, not a switch. A box fades in over blendDistance() as the camera approaches,
 *       because grading that snaps at a boundary reads as a bug even when the two grades are both correct
 */
class [[ToastNode, Icon("Aperture")]] TOAST_API PostProcessVolume : public Volume {
public:
	/// Volume defaults blendDistance to 0, which makes a grade snap at the boundary. A post volume is a
	/// crossfade, so it starts with a metre of blend
	PostProcessVolume() { blendDistance(1.0f); }

	~PostProcessVolume() override = default;

	/// @returns half-extents of the box, meaningless when global
	[[nodiscard]]
	auto extents() const noexcept -> const glm::vec3& {
		return m_extents;
	}

	[[nodiscard]]
	auto isEnabled() const noexcept -> bool {
		return m_enabled;
	}

	/// @returns the grade this volume contributes, at full weight
	///
	/// Rebuilt from the reflected fields on each call, so an inspector edit is visible on the next frame
	/// without the node needing to know it was edited
	[[nodiscard]]
	auto settings() const -> const renderer::PostProcessSettings&;

	/**
	 * @brief How strongly this volume applies with the camera at @p camera_position
	 *
	 * 1 inside the box, falling to 0 across blendDistance() outside it, and always 1 when unbound. Measured
	 * against the box in the volume's own space, so a rotated volume fades along its own axes rather than
	 * along the world's.
	 *
	 * @returns 0 when the volume is disabled or the camera is beyond its influence
	 */
	[[nodiscard]]
	auto influenceAt(const glm::vec3& camera_position) const -> float;

	/// @brief Stores influenceAt(target.position), for callers driving the Volume accumulator interface
	///
	/// The renderer does not: it grades against one camera per frame, so it queries influenceAt() directly
	auto evaluateTarget(const VolumeTarget& target, float weight = 1.0f) -> bool override;

	void resetAccumulators() override;

private:
	/// Half-extents rather than the node's scale, so a volume can be sized without scaling its transform
	[[nodiscard]]
	auto closestPointOnBounds(glm::vec3 point) const -> glm::vec3 override;

	void init();
	void end();
	void destroy();

	[[Reflect, Name("Enabled")]]
	bool m_enabled = true;

	/// Volume supplies Is Global, Priority, Weight and Blend Distance; only the box shape is ours
	[[Reflect, Name("Extents"), Unit("m"), Group("Shape")]] alignas(16) glm::vec3 m_extents = glm::vec3(10.0f);

	[[Reflect, Name("Bloom Threshold"), Range(0.0, 10.0), Group("Bloom")]]
	float m_bloom_threshold = 1.0f;
	[[Reflect, Name("Bloom Knee"), Range(0.0, 1.0), Group("Bloom")]]
	float m_bloom_knee = 0.5f;
	[[Reflect, Name("Bloom Filter Radius"), Range(0.0, 4.0), Group("Bloom")]]
	float m_bloom_filter_radius = 1.0f;
	[[Reflect, Name("Bloom Strength"), Range(0.0, 1.0), Group("Bloom")]]
	float m_bloom_strength = 0.05f;

	[[Reflect, Name("Exposure"), Range(0.0, 8.0), Group("Tonemap")]]
	float m_exposure = 1.0f;
	/// Switches at the halfway point of a blend rather than interpolating - there is no curve in between
	[[Reflect, Name("Tonemap Curve"), Enum("Reinhard", "ACES"), Group("Tonemap")]]
	int32_t m_tonemap_mode = 0;
	[[Reflect, Name("Gamma"), Range(1.0, 3.0), Group("Tonemap")]]
	float m_gamma = 2.2f;
	[[Reflect, Name("Contrast"), Range(0.0, 2.0), Group("Tonemap")]]
	float m_contrast = 1.0f;
	[[Reflect, Name("Saturation"), Range(0.0, 2.0), Group("Tonemap")]]
	float m_saturation = 1.0f;
	[[Reflect, Name("Vignette"), Range(0.0, 1.0), Group("Tonemap")]]
	float m_vignette = 0.0f;
	[[Reflect, Name("Film Grain"), Range(0.0, 1.0), Group("Tonemap")]]
	float m_grain = 0.0f;

	[[Reflect, Name("FXAA Contrast Threshold"), Range(0.0, 0.2), Group("FXAA")]]
	float m_fxaa_contrast_threshold = 0.0312f;
	[[Reflect, Name("FXAA Relative Threshold"), Range(0.0, 0.5), Group("FXAA")]]
	float m_fxaa_relative_threshold = 0.125f;
	[[Reflect, Name("FXAA Subpixel Blending"), Range(0.0, 1.0), Group("FXAA")]]
	float m_fxaa_subpixel_blending = 0.75f;

	[[Reflect, Name("SSR Intensity"), Range(0.0, 2.0), Group("Screen Space Reflections")]]
	float m_ssr_intensity = 1.0f;
	[[Reflect, Name("SSR Max Roughness"), Range(0.0, 1.0), Group("Screen Space Reflections")]]
	float m_ssr_max_roughness = 0.6f;
	[[Reflect, Name("SSR Thickness"), Unit("m"), Range(0.01, 5.0), Group("Screen Space Reflections")]]
	float m_ssr_thickness = 0.5f;
	[[Reflect, Name("SSR Stride"), Unit("m"), Range(0.01, 1.0), Group("Screen Space Reflections")]]
	float m_ssr_stride = 0.15f;
	[[Reflect, Name("SSR Max Steps"), Range(1, 256), Group("Screen Space Reflections")]]
	int32_t m_ssr_max_steps = 48;

	[[Reflect, Name("AO Radius"), Unit("m"), Range(0.01, 10.0), Group("Ambient Occlusion")]]
	float m_ssao_radius = 0.5f;
	[[Reflect, Name("AO Strength"), Range(0.0, 4.0), Group("Ambient Occlusion")]]
	float m_ssao_strength = 1.0f;
	[[Reflect, Name("AO Range Cutoff"), Unit("m"), Range(0.01, 10.0), Group("Ambient Occlusion")]]
	float m_ssao_range_cutoff = 1.0f;
	[[Reflect, Name("AO Samples"), Range(1, 64), Group("Ambient Occlusion")]]
	int32_t m_ssao_sample_count = 16;

	/// @brief Rebuilt from the reflected fields each time the renderer asks
	///
	/// The fields are separate rather than a nested PostProcessSettings because reflection describes flat
	/// members with their own range and group metadata, which is what gives the inspector usable sliders
	mutable renderer::PostProcessSettings m_settings;

	void syncSettings() const;

	/// Last value evaluateTarget() computed; see the note there for why the renderer bypasses it
	float m_influence = 0.0f;

	bool m_registered_proxy = false;
};

}
