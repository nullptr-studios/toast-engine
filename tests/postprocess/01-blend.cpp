#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/renderer/post_process_settings.hpp>

namespace {

auto near(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-4f;
}

}

/// A volume's blend distance is only a visible fade if the fields actually interpolate; the integer ones
/// cannot, so they switch at the halfway point instead
TOAST_TEST_NAMED("postprocess", "postprocess/01-blend", test_postprocess_01_blend) {
	renderer::PostProcessSettings target {};
	renderer::PostProcessSettings source {};

	target.tonemap.exposure = 1.0f;
	source.tonemap.exposure = 3.0f;
	target.ssao.sample_count = 16;
	source.ssao.sample_count = 32;
	target.tonemap.mode = 0;
	source.tonemap.mode = 1;

	// Weight 0 leaves the target exactly as it was - a volume the camera cannot reach contributes nothing
	renderer::PostProcessSettings zero = target;
	renderer::blendPostProcess(zero, source, 0.0f);
	assert(near(zero.tonemap.exposure, 1.0f));
	assert(zero.tonemap.mode == 0);

	// Half way is half way for a float
	renderer::PostProcessSettings half = target;
	renderer::blendPostProcess(half, source, 0.5f);
	assert(near(half.tonemap.exposure, 2.0f));
	assert(half.ssao.sample_count == 24);

	// And the curve switches at exactly that point rather than landing between two curves that do not exist
	assert(half.tonemap.mode == 1);

	renderer::PostProcessSettings just_under = target;
	renderer::blendPostProcess(just_under, source, 0.49f);
	assert(just_under.tonemap.mode == 0);

	// Full weight replaces
	renderer::PostProcessSettings full = target;
	renderer::blendPostProcess(full, source, 1.0f);
	assert(near(full.tonemap.exposure, 3.0f));
	assert(full.ssao.sample_count == 32);

	// Out-of-range weights are clamped rather than extrapolating past the source
	renderer::PostProcessSettings over = target;
	renderer::blendPostProcess(over, source, 4.0f);
	assert(near(over.tonemap.exposure, 3.0f));
}
