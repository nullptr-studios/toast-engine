/// @file Audio.cpp
/// @date 30 Jan 2026

#include "AudioSystem.hpp"

#include <Toast/Audio/Audio.hpp>

namespace audio {

auto load_bank(std::string_view filepath) -> std::expected<void, AudioError> {
	return AudioSystem::get()->load_bank(filepath);
}

auto load_event(std::string_view name, std::span<const std::pair<std::string_view, float>> params) -> std::expected<void, AudioError> {
	return AudioSystem::get()->load_event(name, params);
}

auto set_param(std::string_view event_name, std::string_view param_name, float value) -> std::expected<void, AudioError> {
	return AudioSystem::get()->set_param(event_name, param_name, value);
}

auto play(std::string_view event_name) -> std::expected<void, AudioError> {
	return AudioSystem::get()->play(event_name);
}

auto stop(std::string_view event_name) -> std::expected<void, AudioError> {
	return AudioSystem::get()->stop(event_name);
}

auto is_playing(std::string_view event_name) -> bool {
	return AudioSystem::get()->is_playing(event_name);
}

auto set_volume(std::string_view event_name, float volume) -> std::expected<void, AudioError> {
	return AudioSystem::get()->set_volume(event_name, volume);
}

auto mute_all() -> void {
	AudioSystem::get()->mute_all();
}

auto unmute_all() -> void {
	AudioSystem::get()->unmute_all();
}

auto is_muted() -> bool {
	return AudioSystem::get()->is_muted();
}

namespace core {

auto load(Data& audio_data) -> std::expected<void, AudioError> {
	return AudioSystem::get()->core.load(audio_data);
}

auto play(const Data& audio_data) -> std::expected<void, AudioError> {
	return AudioSystem::get()->core.play(audio_data);
}

auto stop(const Data& audio_data) -> std::expected<void, AudioError> {
	return AudioSystem::get()->core.stop(audio_data);
}

auto update_volume(Data& audio_data, float new_volume, unsigned int fade_length) -> std::expected<void, AudioError> {
	return AudioSystem::get()->core.update_volume(audio_data, new_volume, fade_length);
}

auto update_position(Data& audio_data) -> std::expected<void, AudioError> {
	return AudioSystem::get()->core.update_position(audio_data);
}

auto is_playing(const Data& audio_data) -> bool {
	return AudioSystem::get()->core.is_playing(audio_data);
}

auto set_listener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up) -> void {
	AudioSystem::get()->core.set_listener(pos, forward, up);
}

auto get_length(const Data& audio_data) -> unsigned int {
	return AudioSystem::get()->core.get_length(audio_data);
}

}    // namespace core

}    // namespace audio
