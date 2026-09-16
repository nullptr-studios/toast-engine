#include "post_process_settings.hpp"

#include <algorithm>
#include <cmath>

namespace renderer {
namespace {

auto mix(float a, float b, float t) -> float {
	return a + ((b - a) * t);
}

auto pick(uint32_t a, uint32_t b, float t) -> uint32_t {
	return t >= 0.5f ? b : a;
}

auto mixCount(uint32_t a, uint32_t b, float t) -> uint32_t {
	return static_cast<uint32_t>(std::lround(mix(static_cast<float>(a), static_cast<float>(b), t)));
}

}

void blendPostProcess(PostProcessSettings& target, const PostProcessSettings& source, float weight) {
	const float t = std::clamp(weight, 0.0f, 1.0f);
	if (t <= 0.0f) {
		return;
	}

	target.bloom.threshold = mix(target.bloom.threshold, source.bloom.threshold, t);
	target.bloom.knee = mix(target.bloom.knee, source.bloom.knee, t);
	target.bloom.filter_radius = mix(target.bloom.filter_radius, source.bloom.filter_radius, t);
	target.bloom.strength = mix(target.bloom.strength, source.bloom.strength, t);

	target.tonemap.exposure = mix(target.tonemap.exposure, source.tonemap.exposure, t);
	target.tonemap.mode = pick(target.tonemap.mode, source.tonemap.mode, t);
	target.tonemap.gamma = mix(target.tonemap.gamma, source.tonemap.gamma, t);
	target.tonemap.contrast = mix(target.tonemap.contrast, source.tonemap.contrast, t);
	target.tonemap.saturation = mix(target.tonemap.saturation, source.tonemap.saturation, t);
	target.tonemap.vignette = mix(target.tonemap.vignette, source.tonemap.vignette, t);
	target.tonemap.grain = mix(target.tonemap.grain, source.tonemap.grain, t);

	target.fxaa.contrast_threshold = mix(target.fxaa.contrast_threshold, source.fxaa.contrast_threshold, t);
	target.fxaa.relative_threshold = mix(target.fxaa.relative_threshold, source.fxaa.relative_threshold, t);
	target.fxaa.subpixel_blending = mix(target.fxaa.subpixel_blending, source.fxaa.subpixel_blending, t);

	target.ssr.intensity = mix(target.ssr.intensity, source.ssr.intensity, t);
	target.ssr.max_roughness = mix(target.ssr.max_roughness, source.ssr.max_roughness, t);
	target.ssr.thickness = mix(target.ssr.thickness, source.ssr.thickness, t);
	target.ssr.stride = mix(target.ssr.stride, source.ssr.stride, t);
	target.ssr.max_steps = mixCount(target.ssr.max_steps, source.ssr.max_steps, t);

	target.ssao.radius = mix(target.ssao.radius, source.ssao.radius, t);
	target.ssao.strength = mix(target.ssao.strength, source.ssao.strength, t);
	target.ssao.range_cutoff = mix(target.ssao.range_cutoff, source.ssao.range_cutoff, t);
	target.ssao.sample_count = mixCount(target.ssao.sample_count, source.ssao.sample_count, t);
}

}
