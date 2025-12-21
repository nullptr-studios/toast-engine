/// @file OclussionVolume.hpp
/// @author dario
/// @date 29/10/2025.

#pragma once

#include "DebugDrawLayer.hpp"

#include <algorithm>
#include <glm/glm.hpp>

struct OclussionVolume {
	float mRadius = 5.0f;

	bool isOnFrustumPlanes(const std::array<glm::vec4, 6> planes, const glm::mat4& worldTransform) const;

	static inline bool isSphereOnPlanes(const std::array<glm::vec4, 6> planes, const glm::vec3& center, float radius);

	static inline void extractFrustumPlanesNormalized(const glm::mat4& clip, std::array<glm::vec4, 6>& outPlanes);
};

inline void OclussionVolume::extractFrustumPlanesNormalized(const glm::mat4& clip, std::array<glm::vec4, 6>& outPlanes) {
	outPlanes[0] = glm::vec4(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0],
	                         clip[3][3] + clip[3][0]);    // left
	outPlanes[1] = glm::vec4(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0],
	                         clip[3][3] - clip[3][0]);    // right
	outPlanes[2] = glm::vec4(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1],
	                         clip[3][3] + clip[3][1]);    // bottom
	outPlanes[3] = glm::vec4(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1],
	                         clip[3][3] - clip[3][1]);    // top
	outPlanes[4] = glm::vec4(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2],
	                         clip[3][3] + clip[3][2]);    // near
	outPlanes[5] = glm::vec4(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2],
	                         clip[3][3] - clip[3][2]);    // far

	// Normalize
	for (int i = 0; i < 6; ++i) {
		float len = glm::length(glm::vec3(outPlanes[i]));    // sqrt
		if (len > 1e-9f) {
			outPlanes[i] /= len;
		}
	}
}

inline bool OclussionVolume::isSphereOnPlanes(const std::array<glm::vec4, 6> planes, const glm::vec3& center, float radius) {
	// TODO: Fix oclussion volumes with accurate vertex bounding boxes

	for (int p = 0; p < 6; ++p) {
		const glm::vec4& pl = planes[p];
		float dist = pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w;
		if (dist < -radius) {
			return false;
		}
	}
	return true;
}

inline bool OclussionVolume::isOnFrustumPlanes(const std::array<glm::vec4, 6> planes, const glm::mat4& worldTransform) const {
	glm::vec3 center = glm::vec3(worldTransform[3]);

	float sx = glm::length(glm::vec3(worldTransform[0]));
	float sy = glm::length(glm::vec3(worldTransform[1]));
	float sz = glm::length(glm::vec3(worldTransform[2]));
	float scale = std::max({ sx, sy, sz });
	float radius = mRadius * scale;

	bool visible = isSphereOnPlanes(planes, center, radius);

	return visible;
}
