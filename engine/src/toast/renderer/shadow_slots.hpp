/// @file shadow_slots.hpp
/// @author dario
/// @date 21/09/2026

#pragma once

#include <cstdint>
#include <span>

namespace renderer::shadow_slots {

/// An owner keeps its slot unless outranked by this factor
inline constexpr float k_default_retention = 1.5f;

/// Frames an owner still counts as seen after it leaves the view
inline constexpr uint32_t k_default_grace_frames = 30;

/// Owners keep casting this much past the light shadow distance
inline constexpr float k_distance_slack = 1.15f;

/// Relative camera distance band around a resolution tier switch
inline constexpr float k_resolution_band = 0.1f;

struct Candidate {
	uint64_t key = 0;
	float importance = 0.0f;
	bool visible = false;
};

struct Slot {
	uint64_t key = 0;
	uint32_t hidden_frames = 0;
	uint32_t resolution = 0;
};

/// slot_of is -1 for losers and the return counts owners displaced
auto assign(std::span<const Candidate> candidates, std::span<Slot> slots, std::span<int32_t> slot_of) -> uint32_t;

[[nodiscard]]
auto owns(std::span<const Slot> slots, uint64_t key) -> bool;

[[nodiscard]]
auto retention() -> float;

[[nodiscard]]
auto graceFrames() -> uint32_t;

void setRetention(float factor);
void setGraceFrames(uint32_t frames);

/// Keeps the current tier until the camera distance leaves its band
[[nodiscard]]
auto stableResolution(uint32_t current, float camera_distance, float resolution_scale) -> uint32_t;

}
