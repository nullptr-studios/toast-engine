/**
 * @file aabb.hpp
 * @date 17 Sep 2026
 * @author Xein
 * @brief Axis-aligned bounds node
 */

#pragma once

#include <glm/vec3.hpp>
#include <optional>
#include <toast/export.hpp>

namespace physics {

struct TOAST_API AABB {
	glm::vec3 min {};
	glm::vec3 max {};

	[[nodiscard]]
	auto overlaps(const AABB& other) const -> bool;
	[[nodiscard]]
	auto contains(const AABB& other) const -> bool;
	[[nodiscard]]
	auto expanded(float amount) const -> AABB;
	[[nodiscard]]
	auto area() const -> float;

	struct RayHit {
		float t_min;
		float t_max;
	};

	[[nodiscard]]
	auto intersectRay(const glm::vec3& origin, const glm::vec3& inv_dir, float max_distance) const -> std::optional<RayHit>;
};

[[nodiscard]]
auto combine(const AABB& lhs, const AABB& rhs) -> AABB;

[[nodiscard]]
auto operator+(const AABB& lhs, const AABB& rhs) -> AABB;

}
