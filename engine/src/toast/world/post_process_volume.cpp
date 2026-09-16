#include "post_process_volume.hpp"

#include <algorithm>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <toast/renderer/vulkan_renderer.hpp>

namespace toast {

void PostProcessVolume::init() {
	renderer::registerPostProcessVolumeProxy(this);
	m_registered_proxy = true;
}

void PostProcessVolume::end() {
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterPostProcessVolumeProxy(this);
	m_registered_proxy = false;
}

void PostProcessVolume::destroy() {
	end();
}

void PostProcessVolume::syncSettings() const {
	m_settings.bloom.threshold = m_bloom_threshold;
	m_settings.bloom.knee = m_bloom_knee;
	m_settings.bloom.filter_radius = m_bloom_filter_radius;
	m_settings.bloom.strength = m_bloom_strength;

	m_settings.tonemap.exposure = m_exposure;
	m_settings.tonemap.mode = static_cast<uint32_t>(std::max(m_tonemap_mode, 0));
	m_settings.tonemap.gamma = m_gamma;
	m_settings.tonemap.contrast = m_contrast;
	m_settings.tonemap.saturation = m_saturation;
	m_settings.tonemap.vignette = m_vignette;
	m_settings.tonemap.grain = m_grain;

	m_settings.fxaa.contrast_threshold = m_fxaa_contrast_threshold;
	m_settings.fxaa.relative_threshold = m_fxaa_relative_threshold;
	m_settings.fxaa.subpixel_blending = m_fxaa_subpixel_blending;

	m_settings.ssr.intensity = m_ssr_intensity;
	m_settings.ssr.max_roughness = m_ssr_max_roughness;
	m_settings.ssr.thickness = m_ssr_thickness;
	m_settings.ssr.stride = m_ssr_stride;
	m_settings.ssr.max_steps = static_cast<uint32_t>(std::max(m_ssr_max_steps, 1));

	m_settings.ssao.radius = m_ssao_radius;
	m_settings.ssao.strength = m_ssao_strength;
	m_settings.ssao.range_cutoff = m_ssao_range_cutoff;
	m_settings.ssao.sample_count = static_cast<uint32_t>(std::max(m_ssao_sample_count, 1));
}

auto PostProcessVolume::settings() const -> const renderer::PostProcessSettings& {
	syncSettings();
	return m_settings;
}

auto PostProcessVolume::closestPointOnBounds(glm::vec3 point) const -> glm::vec3 {
	// Volume's own version clamps to a unit cube, so the node's scale is what sizes it. A post volume carries
	// explicit half-extents instead, so that an artist can resize the box without scaling the transform every
	// child would inherit
	syncTransform();
	const glm::mat4& world = getWorldTransform();
	const glm::vec3 half = glm::abs(m_extents);
	const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(point, 1.0f));
	return {world * glm::vec4(glm::clamp(local, -half, half), 1.0f)};
}

auto PostProcessVolume::influenceAt(const glm::vec3& camera_position) const -> float {
	if (!m_enabled) {
		return 0.0f;
	}

	// Volume::calculateWeight handles global, inside and the blend band; the box it measures against is
	// closestPointOnBounds() above, so the falloff runs along the volume's own axes rather than the world's
	return std::clamp(calculateWeight({.position = camera_position, .forward = glm::vec3(0.0f)}), 0.0f, 1.0f);
}

auto PostProcessVolume::evaluateTarget(const VolumeTarget& target, float weight) -> bool {
	m_influence = influenceAt(target.position) * weight;
	return m_influence > 0.0f;
}

void PostProcessVolume::resetAccumulators() {
	m_influence = 0.0f;
}

}
