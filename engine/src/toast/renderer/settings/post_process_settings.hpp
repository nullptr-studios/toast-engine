/**
 * @file post_process_settings.hpp
 * @author dario
 * @date 19/08/2026
 */

#pragma once
#include <cstdint>
#include <toast/assets/texture.hpp>
#include <toast/export.hpp>

namespace renderer {

/// @note Every member must be handled in blendPostProcess()
struct PostProcessSettings {
	struct Bloom {
		float threshold = 1.0f;
		float knee = 0.5f;
		float filter_radius = 1.0f;
		float strength = 0.05f;
	} bloom;

	struct LensFlare {
		float intensity = 0.1f;
		float threshold = 1.0f;
		float threshold_range = 1.0f;
		float ghost_intensity = 1.0f;
		float chroma_shift = 0.015f;
		float halo_intensity = 1.0f;
		float halo_width = 0.6f;
		float halo_mask = 0.5f;
		float halo_compression = 0.65f;
		float glare_intensity = 0.2f;
		float glare_divider = 60.0f;
		float anamorphic_intensity = 1.0f;
		/// Share of the streak pulled toward blue
		float anamorphic_tint = 0.6f;
		float dirt_intensity = 0.0f;
		/// Spread of the ring along its radius
		float halo_thickness = 0.3f;
		float halo_dispersion = 0.015f;
		float halo_tint_r = 1.0f;
		float halo_tint_g = 1.0f;
		float halo_tint_b = 1.0f;
		assets::Handle<assets::Texture> dirt_texture;
	} lens_flare;

	struct Tonemap {
		float exposure = 1.0f;
		/// 0 Reinhard 1 ACES 2 GT7
		uint32_t mode = 0;
		float gt_contrast_dip = 1.28f;
		float chromatic_aberration = 0.0f;
		float gamma = 2.2f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float vignette = 0.0f;
		float grain = 0.0f;

		bool auto_exposure = false;
		/// log2 of the average scene luminance the meter may settle on
		float auto_exposure_min = -8.0f;
		float auto_exposure_max = 4.0f;
		/// Per second toward a brighter scene
		float adapt_speed_up = 3.0f;
		float adapt_speed_down = 1.0f;
	} tonemap;

	struct Fxaa {
		float contrast_threshold = 0.0312f;
		float relative_threshold = 0.125f;
		float subpixel_blending = 0.75f;
	} fxaa;

	struct Ssr {
		float intensity = 1.0f;

		float max_roughness = 0.6f;

		float thickness = 0.5f;

		float stride = 0.15f;

		uint32_t max_steps = 48;
	} ssr;

	struct Ssao {
		float radius = 0.5f;

		float strength = 1.0f;

		float range_cutoff = 1.0f;

		uint32_t sample_count = 16;
	} ssao;

	struct Fog {
		/// Extinction per meter at the base height and 0 turns volumetrics off
		float density = 0.0f;
		float height = 0.0f;
		/// Per meter above the base height and 0 is uniform
		float height_falloff = 0.0f;
		float albedo_r = 1.0f;
		float albedo_g = 1.0f;
		float albedo_b = 1.0f;
		/// Henyey Greenstein g
		float anisotropy = 0.3f;
		float ambient = 1.0f;
		/// Meters a view ray marches before the sky
		float distance = 64.0f;
	} fog;
};

TOAST_API void blendPostProcess(PostProcessSettings& target, const PostProcessSettings& source, float weight);

}
