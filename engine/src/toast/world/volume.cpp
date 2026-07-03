#include "volume.hpp"

using namespace glm;

namespace toast {
auto Volume::isGlobal() const -> bool {
	return m_is_global;
}

void Volume::isGlobal(bool value) {
	m_is_global = value;
}

auto Volume::priority() const -> int8_t {
	return m_priority;
}

void Volume::priority(int8_t value) {
	m_priority = value;
}

auto Volume::weight() const -> float {
	return m_weight;
}

void Volume::weight(float value) {
	m_weight = value;
}

auto Volume::blendDistance() const -> float {
	return m_blend_distance;
}

void Volume::blendDistance(float value) {
	m_blend_distance = value;
}

auto Volume::calculateWeight(const VolumeTarget& target) -> float {
	if (m_is_global) {
		return m_weight;
	}

	vec3 point = closestPointOnBounds(target.position);
	float dist = distance(target.position, point);
	if (dist <= 0.0f) {
		return m_weight;                                       // Object is inside
	}
	if (dist >= m_blend_distance) {
		return 0.0f;                                           // Object is outside
	}
	return m_weight * (1.0f - (dist / m_blend_distance));    // Object is in blend zone
}

auto Volume::closestPointOnBounds(vec3 point) -> vec3 {
	vec3 position_local = inverse(getWorldTransform()) * vec4(point, 1.0f);
	vec3 clamped_point = clamp(position_local, vec3(-.5f), vec3(.5f));
	return getWorldTransform() * vec4(clamped_point, 1.0f);
}

}
