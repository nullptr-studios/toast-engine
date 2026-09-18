/**
 * @file post_process_settings.hpp
 * @author dario
 * @date 19/08/2026
 */

#pragma once
#include <cstdint>
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

	struct Tonemap {
		float exposure = 1.0f;
		/// 0 Reinhard 1 ACES
		uint32_t mode = 0;
		float gamma = 2.2f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float vignette = 0.0f;
		float grain = 0.0f;
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
};

TOAST_API void blendPostProcess(PostProcessSettings& target, const PostProcessSettings& source, float weight);

}
