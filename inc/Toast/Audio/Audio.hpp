/// @file Audio.hpp
/// @date 30 Jan 2026

#pragma once

#include <Toast/Audio/AudioError.hpp>
#include <Toast/Audio/AudioData.hpp>

#include <expected>
#include <glm/vec3.hpp>
#include <span>
#include <string_view>

namespace audio {

/// @brief Loads an FMOD Studio soundbank for event playback.
auto load_bank(std::string_view filepath) -> std::expected<void, AudioError>;

/// @brief Loads an event and optionally applies initial parameter values.
auto load_event(std::string_view name, std::span<const std::pair<std::string_view, float>> params = {}) -> std::expected<void, AudioError>;

/// @brief Sets a parameter value on a loaded event instance.
auto set_param(std::string_view event_name, std::string_view param_name, float value) -> std::expected<void, AudioError>;

/// @brief Starts playback for a loaded event.
auto play(std::string_view event_name) -> std::expected<void, AudioError>;

/// @brief Stops playback for a loaded event.
auto stop(std::string_view event_name) -> std::expected<void, AudioError>;

/// @brief Checks if an event is currently playing.
[[nodiscard]]
auto is_playing(std::string_view event_name) -> bool;

/// @brief Sets the volume for a loaded event.
auto set_volume(std::string_view event_name, float volume) -> std::expected<void, AudioError>;

/// @brief Mutes all audio output.
auto mute_all() -> void;

/// @brief Unmutes all audio output.
auto unmute_all() -> void;

/// @brief Returns whether the audio system is muted.
[[nodiscard]]
auto is_muted() -> bool;

/// @brief Low-level (FMOD Core) API for raw sound playback.
namespace core {
	/// @brief Loads a sound into the core cache.
	auto load(Data& audio_data) -> std::expected<void, AudioError>;

	/// @brief Plays a loaded sound.
	auto play(const Data& audio_data) -> std::expected<void, AudioError>;

	/// @brief Stops a looping sound if currently playing.
	auto stop(const Data& audio_data) -> std::expected<void, AudioError>;

	/// @brief Updates a playing sound's volume, optionally with fade.
	auto update_volume(Data& audio_data, float new_volume, unsigned int fade_length = 0) -> std::expected<void, AudioError>;

	/// @brief Updates a playing sound's 3D position.
	auto update_position(Data& audio_data) -> std::expected<void, AudioError>;

	/// @brief Checks if a looping sound is currently playing.
	[[nodiscard]]
	auto is_playing(const Data& audio_data) -> bool;

	/// @brief Sets the listener transform for 3D audio.
	auto set_listener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up) -> void;

	/// @brief Returns the length of a loaded sound in milliseconds.
	[[nodiscard]]
	auto get_length(const Data& audio_data) -> unsigned int;
}

}    // namespace audio