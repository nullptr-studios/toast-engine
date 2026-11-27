/// @file gi_settings.hpp
/// @author dario
/// @date 22/10/2026

#pragma once

#include <atomic>
#include <cstdint>

namespace renderer {

/// renderer.gi settings written by the settings thread and read by GiClipmapPass on the render thread
struct GiSettings {
	static constexpr uint32_t k_quality_custom = 4;

	std::atomic_bool enabled {true};
	/// Low medium high epic and custom where only custom reads the clipmap knobs below
	std::atomic<uint32_t> quality {2};
	/// Scales bounced light where surfaces are lit and not where GI feeds its own bounces so it cannot run away
	std::atomic<float> intensity {2.0f};
	/// Meters per level 0 cell
	std::atomic<float> clip_cell {0.2f};
	std::atomic<uint32_t> clip_resolution {128};
	std::atomic<uint32_t> clip_levels {4};
	/// 0 off and otherwise 1 + the level drawn
	std::atomic<uint32_t> clip_debug_view {0};
	/// 0 albedo 1 emission 2 opacity 3 radiance
	std::atomic<uint32_t> clip_debug_mode {0};
};

}
