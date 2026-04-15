/// @file Haptics.hpp
/// @date 06 Apr 2026

#pragma once

#include <cstdint>

namespace haptics {

/// @brief Sends dual-motor rumble to the connected gamepad
/// Low frequency is the heavy/left motor, high frequency is the light/right motor
/// Intensities are normalized [0.0, 1.0]
auto Rumble(float low_intensity, float high_intensity, uint32_t duration_ms) -> void;

/// @brief Sends rumble to the gamepad's triggers
/// Not all controllers support this
auto TriggerRumble(float left_intensity, float right_intensity, uint32_t duration_ms) -> void;

/// @brief Stops all active haptics immediately
auto Stop() -> void;

// Preset patterns for common game feedback scenarios

auto ImpactLight() -> void;
auto ImpactHeavy() -> void;
auto Explosion() -> void;
auto Damage() -> void;
auto UIConfirm() -> void;

// DualSense Adaptive Triggers (PS5 only)

/// Which trigger to apply effects to
enum class Trigger : uint8_t {
	Left,
	Right,
	Both
};

/// @brief Sets continuous resistance on a trigger
/// @param trigger Which triggers to affect
/// @param start_position 0.0 = beginning, 1.0 = end of travel
/// @param strength (0.0 = none, 1.0 = maximum
auto TriggerResistance(Trigger trigger, float start_position, float strength) -> void;

/// @brief Sets a resistance "wall" with click-through
/// @param trigger Which triggers to affect
/// @param click_position 0.0-1.0
/// @param strength 0.0-1.0
auto TriggerClick(Trigger trigger, float click_position, float strength) -> void;

/// @brief Sets bouncing recoil
/// @param trigger Which triggesxs to affect
/// @param start_position 0.0-1.0
/// @param strength 0.0-1.0
/// @param pulse_ms Milliseconds between pulses
auto TriggerWeapon(Trigger trigger, float start_position, float strength, uint32_t pulse_ms) -> void;

/// @brief Clears adaptive trigger effects, returning to normal behavior
/// @param trigger Which triggers to clear
auto TriggerClear(Trigger trigger) -> void;

/// @brief Clears all adaptive trigger effects on both triggers.
auto TriggerClearAll() -> void;

}
