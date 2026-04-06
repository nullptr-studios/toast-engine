/// @file Haptics.cpp
/// @date 06 Apr 2026

#include "InputSystem.hpp"

#include <Toast/Input/Haptics.hpp>
#include <Toast/Log.hpp>

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace haptics {

namespace {

auto to_sdl_intensity(float value) -> Uint16 {
	const float clamped = std::clamp(value, 0.0f, 1.0f);
	return static_cast<Uint16>(clamped * 65535.0f);
}

auto to_byte(float value) -> uint8_t {
	const float clamped = std::clamp(value, 0.0f, 1.0f);
	return static_cast<uint8_t>(clamped * 255.0f);
}

auto is_dualsense() -> bool {
	const auto type = input::InputSystem::GetGamepadType();
	return type == SDL_GAMEPAD_TYPE_PS5;
}

// DS5 effect packet structure
struct DS5EffectPacket {
	uint8_t ucEnableBits1 = 0;               // 0 - which features to enable
	uint8_t ucEnableBits2 = 0;               // 1
	uint8_t ucRumbleRight = 0;               // 2
	uint8_t ucRumbleLeft = 0;                // 3
	uint8_t ucHeadphoneVolume = 0;           // 4
	uint8_t ucSpeakerVolume = 0;             // 5
	uint8_t ucMicrophoneVolume = 0;          // 6
	uint8_t ucAudioEnableBits = 0;           // 7
	uint8_t ucMicLightMode = 0;              // 8
	uint8_t ucAudioMuteBits = 0;             // 9
	uint8_t rgucRightTriggerEffect[11] = {}; // 10-20
	uint8_t rgucLeftTriggerEffect[11] = {};  // 21-31
	uint8_t rgucUnknown1[6] = {};            // 32-37
	uint8_t ucEnableBits3 = 0;               // 38
	uint8_t rgucUnknown2[2] = {};            // 39-40
	uint8_t ucLedAnim = 0;                   // 41
	uint8_t ucLedBrightness = 0;             // 42
	uint8_t ucPadLights = 0;                 // 43
	uint8_t ucLedRed = 0;                    // 44
	uint8_t ucLedGreen = 0;                  // 45
	uint8_t ucLedBlue = 0;                   // 46
};

// Trigger effect modes from DS5 protocol
constexpr uint8_t DS5_TRIGGER_MODE_OFF = 0x05;       // Turn off and reset
constexpr uint8_t DS5_TRIGGER_MODE_FEEDBACK = 0x21; // Continuous resistance from a point
constexpr uint8_t DS5_TRIGGER_MODE_WEAPON = 0x25;   // Resistance with "click" at end (single shot)
constexpr uint8_t DS5_TRIGGER_MODE_VIBRATION = 0x26;// Continuous vibration at a frequency
constexpr uint8_t DS5_TRIGGER_MODE_MACHINE = 0x27;  // Oscillating vibration (perfect for recoil bounce)

// Enable bits for trigger effects
constexpr uint8_t DS5_ENABLE_RIGHT_TRIGGER = 0x04;
constexpr uint8_t DS5_ENABLE_LEFT_TRIGGER = 0x08;

/// Sends a raw effect packet to the DualSense controller
auto send_ds5_effect(const DS5EffectPacket& packet) -> bool {
	auto* gamepad = input::InputSystem::GetFirstConnectedGamepad();
	if (not gamepad) {
		return false;
	}

	// SDL_SendGamepadEffect sends raw HID data to the controller
	return SDL_SendGamepadEffect(gamepad, &packet, sizeof(packet));
}

/// Applies a trigger effect to the specified trigger
auto apply_trigger_effect(Trigger trigger, const uint8_t effect[11]) -> void {
	if (not is_dualsense()) {
		TOAST_INFO("Haptics: adaptive triggers only work on DualSense controllers");
		return;
	}

	DS5EffectPacket packet {};

	if (trigger == Trigger::Left or trigger == Trigger::Both) {
		packet.ucEnableBits1 |= DS5_ENABLE_LEFT_TRIGGER;
		std::memcpy(packet.rgucLeftTriggerEffect, effect, 11);
	}
	if (trigger == Trigger::Right or trigger == Trigger::Both) {
		packet.ucEnableBits1 |= DS5_ENABLE_RIGHT_TRIGGER;
		std::memcpy(packet.rgucRightTriggerEffect, effect, 11);
	}

	if (not send_ds5_effect(packet)) {
		TOAST_WARN("Haptics: failed to send DualSense trigger effect");
	}
}

}

auto Rumble(float low_intensity, float high_intensity, uint32_t duration_ms) -> void {
	auto* gamepad = input::InputSystem::GetFirstConnectedGamepad();
	if (not gamepad) {
		TOAST_WARN("Haptics: no controller connected");
		return;
	}

	const Uint16 low = to_sdl_intensity(low_intensity);
	const Uint16 high = to_sdl_intensity(high_intensity);

	if (not SDL_RumbleGamepad(gamepad, low, high, duration_ms)) {
		TOAST_WARN("Haptics rumble failed: {}", SDL_GetError());
	}
}

auto TriggerRumble(float left_intensity, float right_intensity, uint32_t duration_ms) -> void {
	auto* gamepad = input::InputSystem::GetFirstConnectedGamepad();
	if (not gamepad) {
		TOAST_WARN("Haptics: no controller connected");
		return;
	}

	const Uint16 left = to_sdl_intensity(left_intensity);
	const Uint16 right = to_sdl_intensity(right_intensity);

	if (not SDL_RumbleGamepadTriggers(gamepad, left, right, duration_ms)) {
		// Trigger rumble may not be supported
	}
}

auto Stop() -> void {
	auto* gamepad = input::InputSystem::GetFirstConnectedGamepad();
	if (not gamepad) {
		return;    // Nothing to stop
	}

	// Zero intensity stops all rumble
	SDL_RumbleGamepad(gamepad, 0, 0, 0);
	SDL_RumbleGamepadTriggers(gamepad, 0, 0, 0);

	// Also clear adaptive triggers if on DualSense
	if (is_dualsense()) {
		TriggerClearAll();
	}
}

// Preset patterns

auto ImpactLight() -> void {
	// mostly high frequency for snappy feel
	Rumble(0.2f, 0.6f, 80);
}

auto ImpactHeavy() -> void {
	// more low frequency for that bass punch
	Rumble(0.8f, 0.3f, 200);
}

auto Explosion() -> void {
	// both motors going hard
	Rumble(1.0f, 0.7f, 400);
}

auto Damage() -> void {
	// Sharp pulse
	Rumble(0.5f, 0.8f, 120);
}

auto UIConfirm() -> void {
	// Subtle feedback
	Rumble(0.0f, 0.3f, 50);
}

// DualSense Adaptive Trigger Implementation
// based on https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db

auto TriggerResistance(Trigger trigger, float start_position, float strength) -> void {
	uint8_t effect[11] = {};

	// Feedback mode
	// Uses zone bitmasks for precise control
	const uint8_t zone = static_cast<uint8_t>(start_position * 9.0f);  // 0-9 zones
	const uint8_t force = static_cast<uint8_t>(std::clamp(strength * 7.0f, 0.0f, 7.0f)); // 0-7 force

	// Build zone bitmask and force values
	uint16_t active_zones = 0;
	uint32_t force_zones = 0;
	for (int i = zone; i < 10; ++i) {
		active_zones |= static_cast<uint16_t>(1 << i);
		force_zones |= static_cast<uint32_t>(force << (3 * i));
	}

	effect[0] = DS5_TRIGGER_MODE_FEEDBACK;
	effect[1] = static_cast<uint8_t>(active_zones & 0xFF);
	effect[2] = static_cast<uint8_t>((active_zones >> 8) & 0xFF);
	effect[3] = static_cast<uint8_t>(force_zones & 0xFF);
	effect[4] = static_cast<uint8_t>((force_zones >> 8) & 0xFF);
	effect[5] = static_cast<uint8_t>((force_zones >> 16) & 0xFF);
	effect[6] = static_cast<uint8_t>((force_zones >> 24) & 0xFF);

	apply_trigger_effect(trigger, effect);
}

auto TriggerClick(Trigger trigger, float click_position, float strength) -> void {
	uint8_t effect[11] = {};

	const uint8_t start_zone = static_cast<uint8_t>(std::clamp(click_position * 9.0f, 2.0f, 7.0f));
	const uint8_t end_zone = static_cast<uint8_t>(std::min(start_zone + 1, 8));
	const uint8_t force = static_cast<uint8_t>(std::clamp(strength * 7.0f, 0.0f, 7.0f));

	// Weapon mode uses zone bitmask with start and end points
	const uint16_t zones = static_cast<uint16_t>((1 << start_zone) | (1 << end_zone));

	effect[0] = DS5_TRIGGER_MODE_WEAPON;
	effect[1] = static_cast<uint8_t>(zones & 0xFF);
	effect[2] = static_cast<uint8_t>((zones >> 8) & 0xFF);
	effect[3] = force;

	apply_trigger_effect(trigger, effect);
}

auto TriggerWeapon(Trigger trigger, float start_position, float strength, uint32_t pulse_ms) -> void {
	uint8_t effect[11] = {};

	// Vibration mode
	const uint8_t zone = static_cast<uint8_t>(std::clamp(start_position * 9.0f, 0.0f, 9.0f));
	const uint8_t amp = static_cast<uint8_t>(std::clamp(strength * 7.0f, 0.0f, 7.0f));

	// Convert milliseconds to Hz: freq = 1000 / ms
	// Clamp to valid range (1-255 Hz), and handle edge cases
	const uint32_t clamped_ms = std::clamp(pulse_ms, 4u, 1000u); // 4ms = 250Hz max, 1000ms = 1Hz min
	const uint8_t freq_hz = static_cast<uint8_t>(1000u / clamped_ms);

	// Build zone bitmask and amplitude values for all zones from start onwards
	uint16_t active_zones = 0;
	uint32_t amp_zones = 0;
	for (int i = zone; i < 10; ++i) {
		active_zones |= static_cast<uint16_t>(1 << i);
		amp_zones |= static_cast<uint32_t>(amp << (3 * i));
	}

	effect[0] = DS5_TRIGGER_MODE_VIBRATION;
	effect[1] = static_cast<uint8_t>(active_zones & 0xFF);
	effect[2] = static_cast<uint8_t>((active_zones >> 8) & 0xFF);
	effect[3] = static_cast<uint8_t>(amp_zones & 0xFF);
	effect[4] = static_cast<uint8_t>((amp_zones >> 8) & 0xFF);
	effect[5] = static_cast<uint8_t>((amp_zones >> 16) & 0xFF);
	effect[6] = static_cast<uint8_t>((amp_zones >> 24) & 0xFF);
	effect[9] = freq_hz;

	apply_trigger_effect(trigger, effect);
}

auto TriggerClear(Trigger trigger) -> void {
	uint8_t effect[11] = {};
	effect[0] = DS5_TRIGGER_MODE_OFF;

	apply_trigger_effect(trigger, effect);
}

auto TriggerClearAll() -> void {
	// Clear both triggers with a single packet for efficiency
	if (not is_dualsense()) {
		return;
	}

	DS5EffectPacket packet {};
	packet.ucEnableBits1 = DS5_ENABLE_LEFT_TRIGGER | DS5_ENABLE_RIGHT_TRIGGER;
	// Set both triggers to OFF mode explicitly
	packet.rgucLeftTriggerEffect[0] = DS5_TRIGGER_MODE_OFF;
	packet.rgucRightTriggerEffect[0] = DS5_TRIGGER_MODE_OFF;

	send_ds5_effect(packet);
}

}
