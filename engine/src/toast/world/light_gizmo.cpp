#include "light_gizmo.hpp"

#include "point_light.hpp"
#include "spotlight.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <toast/renderer/debug/debug_draw.hpp>

namespace toast::light_gizmo {

namespace {

constexpr float k_min_extent = 0.01f;
constexpr float k_min_angle = 0.5f;
constexpr float k_max_angle = 89.0f;
constexpr int k_circle_segments = 40;

struct Basis {
	glm::vec3 origin {0.0f};
	glm::vec3 right {1.0f, 0.0f, 0.0f};
	glm::vec3 forward {0.0f, 1.0f, 0.0f};
	glm::vec3 up {0.0f, 0.0f, 1.0f};
};

auto basisOf(const Light& light) -> Basis {
	light.syncTransform();
	const glm::quat rotation = light.world_rotation;
	return {
	  .origin = light.world_position,
	  .right = rotation * Node3D::world_right,
	  .forward = rotation * Node3D::world_forward,
	  .up = rotation * Node3D::world_up,
	};
}

auto rangeOf(const Light& light) -> float {
	if (light.lightType() == LightType::spot) {
		return std::max(static_cast<const Spotlight&>(light).attenuation(), k_min_extent);
	}
	if (light.lightType() == LightType::point) {
		return std::max(static_cast<const PointLight&>(light).attenuation(), k_min_extent);
	}
	return 0.0f;
}

void circle(const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, float radius, const glm::vec4& color) {
	glm::vec3 previous = center + u * radius;
	for (int i = 1; i <= k_circle_segments; ++i) {
		const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(k_circle_segments);
		const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
		debug::drawLine(previous, next, color);
		previous = next;
	}
}

}

auto handles(const Light& light) -> std::pair<std::array<Handle, k_max_handles>, uint32_t> {
	std::array<Handle, k_max_handles> out {};
	uint32_t count = 0;
	const auto add = [&](GizmoHandle handle, glm::vec3 local) { out[count++] = Handle {.handle = handle, .local = local}; };

	const LightType type = light.lightType();
	if (type != LightType::spot && type != LightType::point) {
		return {out, 0};
	}

	const float range = rangeOf(light);
	add(GizmoHandle::size_pos_y, {0.0f, range, 0.0f});

	if (type == LightType::spot) {
		const auto& spot = static_cast<const Spotlight&>(light);
		add(GizmoHandle::size_pos_x, {std::tan(glm::radians(spot.outerRadius())) * range, range, 0.0f});
		add(GizmoHandle::size_neg_x, {-std::tan(glm::radians(spot.innerRadius())) * range, range, 0.0f});
		return {out, count};
	}

	switch (light.shape()) {
		case LightShape::point: break;
		case LightShape::sphere:
		case LightShape::disc: add(GizmoHandle::size_pos_z, {0.0f, 0.0f, light.sourceRadius()}); break;
		case LightShape::capsule:
			add(GizmoHandle::size_pos_z, {0.0f, 0.0f, light.sourceRadius()});
			add(GizmoHandle::size_neg_y, {0.0f, -0.5f * light.sourceLength(), 0.0f});
			break;
		case LightShape::rect:
			add(GizmoHandle::size_pos_x, {0.5f * light.sourceWidth(), 0.0f, 0.0f});
			add(GizmoHandle::size_pos_z, {0.0f, 0.0f, 0.5f * light.sourceLength()});
			break;
	}
	return {out, count};
}

auto handleDistance(const Light& light, GizmoHandle handle) -> float {
	const auto [entries, count] = handles(light);
	for (uint32_t i = 0; i < count; ++i) {
		if (entries[i].handle == handle) {
			return std::abs(entries[i].local[gizmo_layout::sizeHandleAxis(handle)]);
		}
	}
	return 0.0f;
}

void applyHandleDistance(Light& light, GizmoHandle handle, float distance) {
	distance = std::max(distance, k_min_extent);
	const LightType type = light.lightType();

	if (handle == GizmoHandle::size_pos_y) {
		if (type == LightType::spot) {
			static_cast<Spotlight&>(light).setAttenuation(distance);
		} else if (type == LightType::point) {
			static_cast<PointLight&>(light).setAttenuation(distance);
		}
		return;
	}

	if (type == LightType::spot) {
		auto& spot = static_cast<Spotlight&>(light);
		const float angle = std::clamp(glm::degrees(std::atan(distance / rangeOf(light))), k_min_angle, k_max_angle);
		if (handle == GizmoHandle::size_pos_x) {
			spot.setOuterRadius(angle);
			spot.setInnerRadius(std::min(spot.innerRadius(), angle));
		} else if (handle == GizmoHandle::size_neg_x) {
			spot.setInnerRadius(std::min(angle, spot.outerRadius()));
		}
		return;
	}

	const bool rect = light.shape() == LightShape::rect;
	switch (handle) {
		case GizmoHandle::size_pos_z:
			if (rect) {
				light.setSourceLength(2.0f * distance);
			} else {
				light.setSourceRadius(distance);
			}
			break;
		case GizmoHandle::size_neg_y: light.setSourceLength(2.0f * distance); break;
		case GizmoHandle::size_pos_x: light.setSourceWidth(2.0f * distance); break;
		default: break;
	}
}

auto handleField(const Light& light, GizmoHandle handle) -> const char* {
	const LightType type = light.lightType();
	if (handle == GizmoHandle::size_pos_y) {
		return "m_attenuation";
	}
	if (type == LightType::spot) {
		if (handle == GizmoHandle::size_pos_x) {
			return "m_outer_radius";
		}
		return handle == GizmoHandle::size_neg_x ? "m_inner_radius" : nullptr;
	}
	switch (handle) {
		case GizmoHandle::size_pos_z: return light.shape() == LightShape::rect ? "m_source_length" : "m_source_radius";
		case GizmoHandle::size_neg_y: return "m_source_length";
		case GizmoHandle::size_pos_x: return "m_source_width";
		default: return nullptr;
	}
}

void draw(const Light& light) {
	const LightType type = light.lightType();
	const Basis basis = basisOf(light);
	const glm::vec4 color(light.color(), 1.0f);
	const glm::vec4 faint(light.color() * 0.5f, 1.0f);

	switch (type) {
		case LightType::directional: debug::drawArrow(basis.origin, basis.origin + basis.forward * 2.0f, color); return;
		case LightType::spot: {
			const auto& spot = static_cast<const Spotlight&>(light);
			const float range = rangeOf(light);
			debug::drawCone(basis.origin, basis.forward, range, spot.outerRadius(), color);
			debug::drawCone(basis.origin, basis.forward, range, std::min(spot.innerRadius(), spot.outerRadius()), faint);
			break;
		}
		case LightType::point: debug::drawSphere(basis.origin, rangeOf(light), faint); break;
		default: return;
	}

	const float radius = light.sourceRadius();
	switch (light.shape()) {
		case LightShape::point: break;
		case LightShape::sphere: debug::drawSphere(basis.origin, radius, color); break;
		case LightShape::capsule: {
			// The capsule helper runs along local z so z takes the light forward axis
			glm::mat4 transform(1.0f);
			transform[0] = glm::vec4(basis.right, 0.0f);
			transform[1] = glm::vec4(glm::cross(basis.forward, basis.right), 0.0f);
			transform[2] = glm::vec4(basis.forward, 0.0f);
			transform[3] = glm::vec4(basis.origin, 1.0f);
			debug::drawCapsule(transform, radius, light.sourceLength() + (2.0f * radius), color, false);
			break;
		}
		case LightShape::disc:
			circle(basis.origin, basis.right, basis.up, radius, color);
			debug::drawArrow(basis.origin, basis.origin + basis.forward * std::max(radius, 0.3f), color, 0.08f);
			break;
		case LightShape::rect: {
			const glm::vec3 w = basis.right * (0.5f * light.sourceWidth());
			const glm::vec3 h = basis.up * (0.5f * light.sourceLength());
			debug::drawLine(basis.origin - w - h, basis.origin + w - h, color);
			debug::drawLine(basis.origin + w - h, basis.origin + w + h, color);
			debug::drawLine(basis.origin + w + h, basis.origin - w + h, color);
			debug::drawLine(basis.origin - w + h, basis.origin - w - h, color);
			debug::drawArrow(basis.origin, basis.origin + basis.forward * 0.4f, color, 0.08f);
			break;
		}
	}
}

}
