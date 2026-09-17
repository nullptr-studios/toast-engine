#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <toast/world/post_process_volume.hpp>

namespace {

auto near(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-4f;
}

}

/// The falloff is what makes a volume a crossfade rather than a switch. Measured as the distance to the
/// closest point on the box, which is Volume::calculateWeight's rule and so the same one the audio volumes
/// blend by
TOAST_TEST_NAMED("postprocess", "postprocess/02-volume-influence", test_postprocess_02_volume_influence) {
	toast::PostProcessVolume volume;
	volume.position = {0.0f, 0.0f, 0.0f};

	// Defaults: a 10m box with a 1m blend distance
	assert(!volume.isGlobal());
	assert(near(volume.blendDistance(), 1.0f));

	// Inside is full weight, and so is the boundary itself
	assert(near(volume.influenceAt({0.0f, 0.0f, 0.0f}), 1.0f));
	assert(near(volume.influenceAt({9.9f, 0.0f, 0.0f}), 1.0f));

	// Halfway through the blend band
	assert(near(volume.influenceAt({10.5f, 0.0f, 0.0f}), 0.5f));

	// Past it, nothing
	assert(near(volume.influenceAt({11.5f, 0.0f, 0.0f}), 0.0f));

	// A corner measures along the diagonal to the box's corner, so it fades sooner than a face does - the
	// blend band is a uniform shell around the box rather than a per-axis one
	const float diagonal = std::sqrt(3.0f) * 0.5f;
	assert(near(volume.influenceAt({10.5f, 10.5f, 10.5f}), 1.0f - diagonal));
}
