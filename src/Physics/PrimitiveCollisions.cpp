#include "Engine/Physics/PrimitiveCollisions.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "../inc/Engine/Physics/Colliders/BoxCollider.hpp"
#include "../inc/Engine/Physics/Colliders/CircleCollider.hpp"
#include "glm/gtx/matrix_interpolation.hpp"
#include "glm/gtx/matrix_transform_2d.hpp"
#include "glm/gtx/norm.hpp"
// #include "../inc/Engine/Physics/Colliders/MeshCollider.hpp"
#include "PhysicsSystem.hpp"
#include "glm/gtx/closest_point.hpp"

#include <iostream>

namespace physics {
using namespace glm;

bool PointInCircle(vec2 point, vec2 circle_center, float radius) {
	return length2(point - circle_center) < radius * radius;
}

bool PointInAbb(vec2 point, vec2 rect, vec2 scale_1) {
	vec2 max_pt = { (scale_1.x / 2) + rect.x, (scale_1.y / 2) + rect.y };
	return point.x < max_pt.x && point.x > max_pt.x - scale_1.x && point.y > max_pt.y - scale_1.y && point.y < max_pt.y;
}

bool PointInObb(vec2 point, vec2 rect, vec2 size, float angle) {
	mat3x3 inv_rot = mat3x3();
	mat3x3 inv_translate = mat3x3();
	inv_rot = rotate(inv_rot, -angle);
	inv_translate = translate(inv_translate, -rect);
	mat3x3 inv_mtw = inv_rot * inv_translate;

	vec3 rect_3d = vec3(rect.x, rect.y, 0.0f);
	rect_3d = inv_mtw * rect_3d;
	vec3 point_3d = vec3(point.x, point.y, 1.0f);
	point_3d = inv_mtw * point_3d;
	point = point_3d;
	rect = rect_3d;
	return PointInAbb(point, rect, size);
}

std::optional<ContactInfo> CircleToCircle(vec2 position_1, float radius_1, vec2 position_2, float radius_2) {
	ContactInfo result {};
	if (length2(position_2 - position_1) > (radius_1 + radius_2) * (radius_1 + radius_2)) {
		return std::nullopt;
	}

	vec2 inv_normal = position_1 - position_2;

	result.normal = normalize(inv_normal);
	result.penetration = (radius_1 + radius_2) - length(inv_normal);
	result.intersection = position_1 + result.normal * radius_1;
	result.normal *= -1;

	return result;
}

std::optional<ContactInfo> ObbToObb(vec2 position_1, vec2 scale_1, float angle_1, vec2 position_2, vec2 scale_2, float angle_2) {
	ContactInfo contact_info {};
	vec2 distance_vector = position_2 - position_1;
	std::array<vec2, 4> axes {};
	axes[0] = vec2(cos(angle_1), sin(angle_1));
	axes[1] = vec2(-sin(angle_1), cos(angle_1));
	axes[2] = vec2(cos(angle_2), sin(angle_2));
	axes[3] = vec2(-sin(angle_2), cos(angle_2));

	std::array<vec2, 4> mid_edges {};
	std::array<float, 4> half_scales { scale_1.x * 0.5f, scale_1.y * 0.5f, scale_2.x * 0.5f, scale_2.y * 0.5f };

	for (unsigned int i = 0; i < 4; ++i) {
		mid_edges[i] = axes[i] * half_scales[i];
	}

	std::array<float, 4> projections {}, projection_length {};
	float min_value = std::numeric_limits<float>::max();
	unsigned int min_index = 0;
	for (unsigned int i = 0; i < 4; ++i) {
		projections[i] = std::abs(dot(distance_vector, axes[i]));
		projection_length[i] = 0.0f;

		for (unsigned int j = 0; j < 4; ++j) {
			if (i == j) {
				projection_length[i] += half_scales[j];
			} else {
				projection_length[i] += std::abs(dot(mid_edges[j], axes[i]));
			}
		}

		double axis_depth = projections[i] - projection_length[i];
		if (axis_depth > 0.0) {
			return std::nullopt;
		}

		axis_depth = std::abs(axis_depth);
		if (axis_depth < min_value) {
			min_value = axis_depth;
			min_index = i;
			contact_info.penetration = axis_depth;
		}
	}

	contact_info.normal = axes[min_index];
	contact_info.penetration /= length(distance_vector);
	contact_info.normal /= length(contact_info.normal);
	if (dot(contact_info.normal, distance_vector) < 0.0f) {
		contact_info.normal *= -1.0f;
	}

	return contact_info;
}

std::optional<RayCastResult> RayToCircle(vec2 point, vec2 direction, vec2 center, float radius) {
	RayCastResult result {};
	vec2 closest_point = (point - center) * direction;

	if (length2(closest_point - point) > radius * radius) {
		return std::nullopt;
	}

	result.position = closest_point;
	result.direction = vec2(direction.y, -direction.x);
	return result;
}

std::optional<RayCastResult> RayToObb(vec2 point, vec2 direction, vec2 position, vec2 scale, float angle, float limit) {
	RayCastResult result {};

	vec2 min { position.x - (scale.x * 0.5f), position.y - (scale.y * 0.5f) };

	vec2 max { position.x + (scale.x * 0.5f), position.y + (scale.y * 0.5f) };

	vec2 t_start = (min - point) / direction;
	vec2 t_end = (max - point) / direction;

	vec2 ray_min;
	vec2 ray_max;

	ray_min.x = std::isnan(t_start.x) || std::isnan(t_end.x) ? -INFINITY : glm::min(t_start.x, t_end.x);
	ray_min.y = std::isnan(t_start.y) || std::isnan(t_end.y) ? -INFINITY : glm::min(t_start.y, t_end.y);

	ray_max.x = std::isnan(t_start.x) || std::isnan(t_end.x) ? INFINITY : glm::max(t_start.x, t_end.x);
	ray_max.y = std::isnan(t_start.y) || std::isnan(t_end.y) ? INFINITY : glm::max(t_start.y, t_end.y);

	float t_min = glm::max(ray_min.x, ray_min.y);
	float t_max = glm::min(ray_max.x, ray_max.y);

	if (t_min > t_max || t_min < 0.0) {
		return std::nullopt;
	}

	result.position = point + t_min * direction;
	if (length2(result.position - point) > limit * limit) {
		return std::nullopt;
	}
	result.direction = direction;
	return result;
}

std::optional<ContactInfo> Collide(ICollider* collider1, ICollider* collider2) {
	if (collider1->collider_type() != collider2->collider_type()) {
		return std::nullopt;
	}

	std::optional<ContactInfo> contact {};
	if (collider1->collider_type() == ICollider::ColliderType::Box) {
		const BoxCollider* box1 = static_cast<BoxCollider*>(collider1);
		const BoxCollider* box2 = static_cast<BoxCollider*>(collider2);
		contact =
		    ObbToObb(box1->GetPosition(), box1->GetSize(), box1->GetRotation(false), box2->GetPosition(), box2->GetSize(), box2->GetRotation(false));

		return contact;
	}

	if (collider1->collider_type() == ICollider::ColliderType::Circle) {
		const CircleCollider* circle1 = static_cast<CircleCollider*>(collider2);
		const CircleCollider* circle2 = static_cast<CircleCollider*>(collider1);
		contact = CircleToCircle(circle1->GetPosition(), circle1->GetRadius(), circle2->GetPosition(), circle2->GetRadius());

		return contact;
	}

	return std::nullopt;
}

std::optional<RayCastResult> Collide(const vec2 position, const vec2 direction, ICollider* collider, ColliderFlags ray_flag, float limit) {
	std::optional<RayCastResult> ray {};

	if (collider->collider_type() == ICollider::ColliderType::Box) {
		auto* box = static_cast<BoxCollider*>(collider);
		if ((static_cast<char>(ray_flag) & static_cast<char>(box->flags)) == 0) {
			return std::nullopt;
		}

		ray = RayToObb(position, direction, box->GetPosition(), box->GetSize(), box->GetRotation(false), limit);
		if (ray != std::nullopt) {
			ray->collider = box;
			return ray;
		}
	}

	if (collider->collider_type() == ICollider::ColliderType::Circle) {
		auto* circle = static_cast<CircleCollider*>(collider);
		ray = RayToCircle(position, direction, circle->GetPosition(), circle->GetRadius());
		if (ray != std::nullopt) {
			ray->collider = circle;
			return ray;
		}
	}
	return std::nullopt;
}
}
