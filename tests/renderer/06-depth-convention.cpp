#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/renderer/core/depth_convention.hpp>
#include <toast/renderer/lighting/frustum.hpp>

// Shadow maps keep standard depth and are not covered
TOAST_TEST_NAMED("Renderer", "renderer/06-depth-convention", test_renderer_06_depth_convention) {
	namespace depth = renderer::depth;

	constexpr float near_plane = 0.1f;
	constexpr float far_plane = 1000.0f;
	const glm::mat4 projection = depth::perspective(glm::radians(60.0f), 16.0f / 9.0f, near_plane, far_plane);

	const auto ndc_depth = [&](float view_distance) {
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -view_distance, 1.0f);
		return clip.z / clip.w;
	};
	const auto close = [](float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; };

	assert(close(ndc_depth(near_plane), depth::k_near, 1e-5f));
	assert(close(ndc_depth(far_plane), depth::k_far, 1e-5f));
	assert(depth::k_clear == depth::k_far);

	const bool nearer_is_greater = ndc_depth(10.0f) > ndc_depth(20.0f);
	assert(nearer_is_greater == depth::k_reversed_z);
	if constexpr (depth::k_reversed_z) {
		assert(depth::k_closer == vk::CompareOp::eGreater);
		assert(depth::k_closer_or_equal == vk::CompareOp::eGreaterOrEqual);

		// Standard depth puts 500 m and 501 m within a few float ulps of 1
		assert(ndc_depth(500.0f) - ndc_depth(501.0f) > 1e-7f);
	} else {
		assert(depth::k_closer == vk::CompareOp::eLess);
		assert(depth::k_closer_or_equal == vk::CompareOp::eLessOrEqual);
	}

	const auto planes = renderer::extractFrustumPlanes(projection, depth::k_reversed_z);
	assert(renderer::sphereInFrustum(planes, glm::vec3(0.0f, 0.0f, -50.0f), 0.5f));
	assert(!renderer::sphereInFrustum(planes, glm::vec3(0.0f, 0.0f, 50.0f), 0.5f));
	assert(!renderer::sphereInFrustum(planes, glm::vec3(0.0f, 0.0f, -2000.0f), 0.5f));

	// Index 4 is the near plane in either convention
	const glm::vec4 near_plane_eq = planes[4];
	assert(near_plane_eq.z < 0.0f);
	assert(close(-near_plane_eq.w / near_plane_eq.z, -near_plane, 1e-3f));

	// Camera::screenPointToRay relies on near to far pointing away from the camera
	const glm::mat4 inverse = glm::inverse(projection);
	glm::vec4 near_point = inverse * glm::vec4(0.0f, 0.0f, depth::k_near, 1.0f);
	glm::vec4 far_point = inverse * glm::vec4(0.0f, 0.0f, depth::k_far, 1.0f);
	near_point /= near_point.w;
	far_point /= far_point.w;
	assert(far_point.z < near_point.z);
}
