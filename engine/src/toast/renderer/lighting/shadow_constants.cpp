#include "toast/renderer/lighting/shadow_constants.hpp"

#include <algorithm>
#include <atomic>

namespace renderer::shadows {
namespace {

std::atomic<uint32_t> g_cascade_resolution {k_cascade_resolution};
std::atomic<uint32_t> g_punctual_resolution {k_punctual_resolution};
std::atomic<float> g_shadow_distance {k_shadow_distance};
std::atomic<uint32_t> g_spot_slots {k_default_spot_shadows};
std::atomic<uint32_t> g_point_slots {k_default_point_shadows};
std::atomic<uint32_t> g_spot_budget {k_max_spot_shadows};
std::atomic<uint32_t> g_point_budget {k_max_point_shadows};
std::atomic<float> g_full_resolution_ranges {k_punctual_full_resolution_ranges};
std::atomic<float> g_split_lambda {k_cascade_split_lambda};
std::atomic<float> g_cascade_padding {k_cascade_padding};
std::atomic<float> g_receiver_depth_radii {k_cascade_receiver_depth_radii};
std::atomic_bool g_static_caster_cache {true};
std::atomic_bool g_voxel_mesh_shadows {true};
std::atomic<float> g_caster_tolerance {k_caster_tolerance};

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

auto spotShadowSlots() -> uint32_t {
	return g_spot_slots.load(std::memory_order_relaxed);
}

auto pointShadowSlots() -> uint32_t {
	return g_point_slots.load(std::memory_order_relaxed);
}

void setSpotShadowSlots(uint32_t slots) {
	g_spot_slots.store(std::min(slots, k_max_spot_shadows), std::memory_order_relaxed);
}

void setPointShadowSlots(uint32_t slots) {
	g_point_slots.store(std::min(slots, k_max_point_shadows), std::memory_order_relaxed);
}

auto spotShadowBudget() -> uint32_t {
	return std::min(g_spot_budget.load(std::memory_order_relaxed), spotShadowSlots());
}

auto pointShadowBudget() -> uint32_t {
	return std::min(g_point_budget.load(std::memory_order_relaxed), pointShadowSlots());
}

auto punctualFullResolutionRanges() -> float {
	return g_full_resolution_ranges.load(std::memory_order_relaxed);
}

auto cascadeSplitLambda() -> float {
	return g_split_lambda.load(std::memory_order_relaxed);
}

auto cascadePadding() -> float {
	return g_cascade_padding.load(std::memory_order_relaxed);
}

auto cascadeReceiverDepthRadii() -> float {
	return g_receiver_depth_radii.load(std::memory_order_relaxed);
}

auto staticCasterCache() -> bool {
	return g_static_caster_cache.load(std::memory_order_relaxed);
}

auto voxelMeshShadows() -> bool {
	return g_voxel_mesh_shadows.load(std::memory_order_relaxed);
}

void setSpotShadowBudget(uint32_t lights) {
	g_spot_budget.store(std::min(lights, k_max_spot_shadows), std::memory_order_relaxed);
}

void setPointShadowBudget(uint32_t lights) {
	g_point_budget.store(std::min(lights, k_max_point_shadows), std::memory_order_relaxed);
}

void setPunctualFullResolutionRanges(float ranges) {
	g_full_resolution_ranges.store(std::max(ranges, 0.0f), std::memory_order_relaxed);
}

void setCascadeSplitLambda(float lambda) {
	g_split_lambda.store(std::clamp(lambda, 0.0f, 1.0f), std::memory_order_relaxed);
}

void setCascadePadding(float padding) {
	g_cascade_padding.store(std::max(padding, 0.0f), std::memory_order_relaxed);
}

void setCascadeReceiverDepthRadii(float radii) {
	// One radius is the far side of the cascade sphere and anything less would clip its own slice
	g_receiver_depth_radii.store(std::max(radii, 1.0f), std::memory_order_relaxed);
}

auto casterTolerance() -> float {
	return g_caster_tolerance.load(std::memory_order_relaxed);
}

void setCasterTolerance(float meters) {
	g_caster_tolerance.store(std::max(meters, 0.0f), std::memory_order_relaxed);
}

void setStaticCasterCache(bool enabled) {
	g_static_caster_cache.store(enabled, std::memory_order_relaxed);
}

void setVoxelMeshShadows(bool enabled) {
	g_voxel_mesh_shadows.store(enabled, std::memory_order_relaxed);
}

}
