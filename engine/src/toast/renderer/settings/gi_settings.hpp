/// @file gi_settings.hpp
/// @author dario
/// @date 22/10/2026

#pragma once

#include <atomic>
#include <cstdint>

namespace renderer {

/// renderer.gi settings written by the settings thread and read by GiPass on the render thread
struct GiSettings {
	static constexpr uint32_t k_quality_custom = 4;

	std::atomic_bool enabled {true};
	/// Low medium high epic and custom where only custom reads the cost knobs below
	std::atomic<uint32_t> quality {2};
	/// GI frames run once every this many rendered frames
	std::atomic<uint32_t> update_interval {1};
	/// Off screen hits kept for the secondary cache each frame
	std::atomic<uint32_t> secondary_points {65536};
	/// Off screen hits seed and read a coarser secondary cache for every bounce past the first
	std::atomic_bool multibounce {true};
	/// Meters between c0 probes at LOD 0
	std::atomic<float> probe_spacing {0.1f};
	/// Chebyshev meters from the camera where LOD 1 starts
	std::atomic<float> lod_distance {4.0f};
	std::atomic<uint32_t> cascades {5};
	/// Pixels per side of the block one probe insertion and later one ray comes from
	std::atomic<uint32_t> ray_step {2};
	std::atomic<uint32_t> probe_lifetime {60};
	/// 0 keeps no history
	std::atomic<float> history_seconds {0.3f};
	/// Blend history_blend per frame instead of history_seconds
	std::atomic_bool history_per_frame {false};
	/// Share of a new frame when history_per_frame is on and 1 keeps no history
	std::atomic<float> history_blend {0.03f};
	/// Meters a body has to move in a frame before GI treats it as a change and not physics jitter
	std::atomic<float> motion_threshold {0.01f};
	std::atomic<float> max_ray_radiance {16.0f};
	/// 0 off and otherwise 1 + the cascade drawn
	std::atomic<uint32_t> debug_view {0};
	/// Mirrors gi_debug.slang kGiDebug with 0 cells 1 interval radiance 2 coverage 3 irradiance 4 history depth 5 change
	std::atomic<uint32_t> debug_mode {0};
};

}
