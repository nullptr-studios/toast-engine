/// @file frustum.hpp
/// @author dario
/// @date 21/09/2026

#pragma once

#include <array>
#include <glm/glm.hpp>
#include <limits>

namespace renderer {

using FrustumPlanes = std::array<glm::vec4, 6>;

struct Box {
	glm::vec3 min {0.0f};
	glm::vec3 max {0.0f};
};

/// Inward normalized planes and with 0 to 1 clip depth near is row 2 alone
[[nodiscard]]
inline auto extractFrustumPlanes(const glm::mat4& view_projection) -> FrustumPlanes {
	const auto& m = view_projection;
	// glm is column major
	const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
	const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
	const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
	const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);

	FrustumPlanes planes {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};

	for (auto& plane : planes) {
		const float length = glm::length(glm::vec3(plane));
		if (length > 0.0f) {
			plane /= length;
		}
	}
	return planes;
}

[[nodiscard]]
inline auto sphereInFrustum(const FrustumPlanes& planes, const glm::vec3& center, float radius) -> bool {
	for (const auto& plane : planes) {
		if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
			return false;
		}
	}
	return true;
}

/// World axis aligned box around a box in the space @p model maps from
[[nodiscard]]
inline auto transformBox(const glm::mat4& model, const glm::vec3& min, const glm::vec3& max) -> Box {
	Box out {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
	for (int corner = 0; corner < 8; ++corner) {
		const glm::vec3 local(
		    (corner & 1) != 0 ? max.x : min.x, (corner & 2) != 0 ? max.y : min.y, (corner & 4) != 0 ? max.z : min.z
		);
		const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
		out.min = glm::min(out.min, world);
		out.max = glm::max(out.max, world);
	}
	return out;
}

/// Skips the near plane so a box in front of it still counts
[[nodiscard]]
inline auto boxTouchesLateralFrustum(const FrustumPlanes& planes, const Box& box) -> bool {
	constexpr size_t k_near = 4;
	for (size_t i = 0; i < planes.size(); ++i) {
		if (i == k_near) {
			continue;
		}
		const glm::vec3 normal(planes[i]);
		const glm::vec3 support(
		    normal.x >= 0.0f ? box.max.x : box.min.x,
		    normal.y >= 0.0f ? box.max.y : box.min.y,
		    normal.z >= 0.0f ? box.max.z : box.min.z
		);
		if (glm::dot(normal, support) + planes[i].w < 0.0f) {
			return false;
		}
	}
	return true;
}

}
