#include "test_registry.hpp"

#include <cassert>
#include <toast/renderer/lighting/shadow_constants.hpp>

namespace {

/// @returns true when @p value is a power of two, which the sub-rect scheme depends on - see
/// renderer::shadows::punctualShadowResolution
auto isPowerOfTwo(uint32_t value) -> bool {
	return value != 0 && (value & (value - 1)) == 0;
}

}

// Punctual tiles stay within the min and max resolution and a power of two
TOAST_TEST_NAMED("Renderer", "renderer/01-shadow-resolution", test_renderer_01_shadow_resolution) {
	using namespace renderer::shadows;
	constexpr float k_range = 10.0f;
	const float reach = k_punctual_full_resolution_ranges * k_range;

	// Within the full resolution reach the light gets the whole tile
	assert(punctualShadowResolution(0.0f, k_range, 1.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(reach, k_range, 1.0f) == k_punctual_resolution);

	// Each doubling of the distance past the reach halves the tile down to the floor
	assert(punctualShadowResolution(reach * 2.0f, k_range, 1.0f) == k_punctual_resolution / 2);
	assert(punctualShadowResolution(100000.0f, k_range, 1.0f) == k_min_punctual_resolution);

	// A wider light keeps its detail further away
	assert(punctualShadowResolution(reach * 2.0f, k_range * 2.0f, 1.0f) == k_punctual_resolution);

	// Monotonically non-increasing with distance, and always a legal power-of-two tile
	uint32_t previous = punctualShadowResolution(0.0f, k_range, 1.0f);
	for (float distance = 0.0f; distance < 500.0f; distance += 0.5f) {
		const uint32_t resolution = punctualShadowResolution(distance, k_range, 1.0f);
		assert(resolution <= previous);
		assert(resolution >= k_min_punctual_resolution);
		assert(resolution <= k_punctual_resolution);
		assert(isPowerOfTwo(resolution));
		previous = resolution;
	}

	// The artist scale only ever reduces, and never below the floor however small it goes
	assert(punctualShadowResolution(0.0f, k_range, 0.5f) < k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, k_range, 0.5f) >= k_min_punctual_resolution);
	assert(punctualShadowResolution(0.0f, k_range, 0.0f) == k_min_punctual_resolution);
	assert(punctualShadowResolution(0.0f, k_range, 1.0f) >= punctualShadowResolution(0.0f, k_range, 0.75f));

	// Out-of-range inputs are clamped rather than extrapolated - a negative distance is what a light sitting
	// exactly on the camera can produce through floating-point noise
	assert(punctualShadowResolution(-5.0f, k_range, 1.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, k_range, 5.0f) == k_punctual_resolution);
	assert(punctualShadowResolution(0.0f, k_range, -1.0f) == k_min_punctual_resolution);
	assert(punctualShadowResolution(10.0f, -1.0f, 1.0f) == k_min_punctual_resolution);
}
