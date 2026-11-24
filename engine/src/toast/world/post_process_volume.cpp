#include "post_process_volume.hpp"

#include <glm/common.hpp>
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

void PostProcessVolume::syncParams() const {
	// Built before the asset manager could load the schema, so it holds no slots and cannot take a value
	const bool rebuild = m_params.layout()->slots.empty();
	if (rebuild) {
		m_params = renderer::PostProcessParams {};
	}

	const assets::PostProcessProfile* profile = m_profile.hasValue() ? &*m_profile : nullptr;
	const uint32_t version = profile != nullptr ? profile->version() : 0;
	if (!rebuild && profile == m_synced_profile && version == m_synced_version) {
		return;
	}

	m_synced_profile = profile;
	m_synced_version = version;
	if (profile != nullptr) {
		m_params.loadFrom(*profile);
	} else {
		m_params.reset();
	}
}

auto PostProcessVolume::params() const -> const renderer::PostProcessParams& {
	syncParams();
	return m_params;
}

auto PostProcessVolume::setParam(std::string_view name, float value) -> bool {
	syncParams();
	return m_params.setNumber(name, value);
}

auto PostProcessVolume::setParamColor(std::string_view name, glm::vec3 value) -> bool {
	syncParams();
	return m_params.setColor(name, value);
}

auto PostProcessVolume::setParamOption(std::string_view name, std::string_view option) -> bool {
	syncParams();
	return m_params.setOption(name, option);
}

auto PostProcessVolume::getParam(std::string_view name) const -> float {
	syncParams();
	return m_params.number(name).value_or(0.0f);
}

auto PostProcessVolume::getParamColor(std::string_view name) const -> glm::vec3 {
	syncParams();
	return m_params.color(name).value_or(glm::vec3(0.0f));
}

auto PostProcessVolume::hasParam(std::string_view name) const -> bool {
	syncParams();
	return m_params.layout()->find(name).has_value();
}

void PostProcessVolume::resetParams() {
	// A version no profile reports forces the next sync to load again
	m_synced_profile = nullptr;
	m_synced_version = ~0u;
	syncParams();
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
