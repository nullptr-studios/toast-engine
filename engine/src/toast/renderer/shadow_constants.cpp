#include "shadow_constants.hpp"

#include <atomic>

namespace renderer::shadows {
namespace {

std::atomic<uint32_t> g_cascade_resolution {k_cascade_resolution};
std::atomic<uint32_t> g_punctual_resolution {k_punctual_resolution};
std::atomic<float> g_shadow_distance {k_shadow_distance};

}

auto cascadeResolution() -> uint32_t {
	return g_cascade_resolution.load(std::memory_order_relaxed);
}

auto punctualResolution() -> uint32_t {
	return g_punctual_resolution.load(std::memory_order_relaxed);
}

auto shadowDistance() -> float {
	return g_shadow_distance.load(std::memory_order_relaxed);
}

void setCascadeResolution(uint32_t resolution) {
	g_cascade_resolution.store(resolution == 0 ? k_cascade_resolution : resolution, std::memory_order_relaxed);
}

void setPunctualResolution(uint32_t resolution) {
	// A ceiling below the minimum would exceed the layer
	g_punctual_resolution.store(
	    resolution < k_min_punctual_resolution ? k_min_punctual_resolution : resolution, std::memory_order_relaxed
	);
}

void setShadowDistance(float distance) {
	g_shadow_distance.store(distance <= 0.0f ? k_shadow_distance : distance, std::memory_order_relaxed);
}

}
