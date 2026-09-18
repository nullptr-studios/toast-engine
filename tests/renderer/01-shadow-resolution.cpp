#include "test_registry.hpp"

#include <cassert>
#include <toast/renderer/shadow_constants.hpp>

namespace {

/// @returns true when @p value is a power of two, which the sub-rect scheme depends on - see
/// renderer::shadows::punctualShadowResolution
auto isPowerOfTwo(uint32_t value) -> bool {
	return value != 0 && (value & (value - 1)) == 0;
}

}

// A punctual light's shadow is rendered into a sub-rect of its layer whose size falls off with camera
// distance. Anything outside [k_min_punctual_resolution, k_punctual_resolution] would either scissor outside
// the attachment or waste the whole tile, and a non-power-of-two would put the light's texel grid out of
// step with the atlas grid
TOAST_TEST_NAMED("Renderer", "renderer/01-shadow-resolution", test_renderer_01_shadow_resolution) {
	using namespace renderer::shadows;

	// Inside the full-resolution radius the light gets the whole tile
	assert(punctualShadowResolution(0.0f, 1.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(k_punctual_full_resolution_distance, 1.0f) == k_punctual_resolution);

	// At and beyond the far end it sits at the floor, including absurd distances
	assert(punctualShadowResolution(k_punctual_min_resolution_distance, 1.0f) == k_min_punctual_resolution);
	assert(punctualShadowResolution(100000.0f, 1.0f) == k_min_punctual_resolution);

	// Monotonically non-increasing with distance, and always a legal power-of-two tile
	uint32_t previous = punctualShadowResolution(0.0f, 1.0f);
	for (float distance = 0.0f; distance < 200.0f; distance += 0.5f) {
		const uint32_t resolution = punctualShadowResolution(distance, 1.0f);
		assert(resolution <= previous);
		assert(resolution >= k_min_punctual_resolution);
		assert(resolution <= k_punctual_resolution);
		assert(isPowerOfTwo(resolution));
		previous = resolution;
	}

	// The artist scale only ever reduces, and never below the floor however small it goes
	assert(punctualShadowResolution(0.0f, 0.5f) < k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, 0.5f) >= k_min_punctual_resolution);
	assert(punctualShadowResolution(0.0f, 0.0f) == k_min_punctual_resolution);
	assert(punctualShadowResolution(0.0f, 1.0f) >= punctualShadowResolution(0.0f, 0.75f));

	// Out-of-range inputs are clamped rather than extrapolated - a negative distance is what a light sitting
	// exactly on the camera can produce through floating-point noise
	assert(punctualShadowResolution(-5.0f, 1.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, 5.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, -1.0f) == k_min_punctual_resolution);
}
