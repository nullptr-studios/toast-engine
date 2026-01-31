/// @file AudioSystem.hpp
/// @author Xein
/// @date 30 Jan 2026

#pragma once
#include <Toast/Audio/AudioData.hpp>
#include <Toast/Audio/AudioError.hpp>
#include <expected>
#include <string_view>
#include <span>
#include <memory>
#include <fmod.hpp>
#include <fmod_studio.hpp>

/**
 * @brief Error Handling Function for FMOD Errors
 * @param result the FMOD_RESULT generated during every FMOD function call
 * @param file the file in which the error occurred
 * @param line the line in which the error occurred
 */
void ERRCHECK_fn(FMOD_RESULT result, const char* file, int line);
#define ERRCHECK(_result) ERRCHECK_fn(_result, __FILE__, __LINE__)

namespace audio {

class AudioSystem {
public:
	static auto create() -> std::expected<AudioSystem*, AudioError>;
	static auto get() noexcept -> AudioSystem*;

	void Init();
	void Tick() const;
	void Destroy() const;

	// FMOD Studio Event System
	/// @brief Loads an FMOD Studio soundbank (*.bank) file
	auto load_bank(std::string_view filepath) -> std::expected<void, AudioError>;

	/// @brief Loads an FMOD Studio Event with optional initial parameters
	auto load_event(std::string_view name, std::span<const std::pair<std::string_view, float>> params = {}) -> std::expected<void, AudioError>;

	/// @brief Sets a parameter value for a loaded event
	auto set_param(std::string_view event_name, std::string_view param_name, float value) -> std::expected<void, AudioError>;

	/// @brief Plays a loaded event
	auto play(std::string_view event_name) -> std::expected<void, AudioError>;

	/// @brief Stops a playing event with fade-out
	auto stop(std::string_view event_name) -> std::expected<void, AudioError>;

	/// @brief Checks if an event is currently playing
	[[nodiscard]]
	auto is_playing(std::string_view event_name) const -> bool;

	/// @brief Sets the volume for an event
	auto set_volume(std::string_view event_name, float volume) -> std::expected<void, AudioError>;

	// Audio Control
	[[nodiscard]]
	auto is_muted() const noexcept -> bool;
	
	auto mute_all() -> AudioSystem&;
	auto unmute_all() -> AudioSystem&;

	// The audio sampling rate of the audio engine
	static constexpr int AUDIO_SAMPLE_RATE = 48000;

	// FMOD Core Low-Level Audio System
	struct CoreSystem {
		AudioSystem* owner = nullptr;
		explicit CoreSystem(AudioSystem* system) : owner(system) {}

		/// @brief Loads a sound from disk with specified settings
		auto load(Data& audio_data) -> std::expected<void, AudioError>;

		/// @brief Plays a previously loaded sound
		auto play(const Data& audio_data) -> std::expected<void, AudioError>;

		/// @brief Stops a looping sound
		auto stop(const Data& audio_data) -> std::expected<void, AudioError>;

		/// @brief Updates the volume of a playing sound with optional fade
		auto update_volume(Data& audio_data, float new_volume, unsigned int fade_length = 0) -> std::expected<void, AudioError>;

		/// @brief Updates the 3D position of a playing sound
		auto update_position(Data& audio_data) -> std::expected<void, AudioError>;

		/// @brief Checks if a looping sound is currently playing
		[[nodiscard]]
		auto is_playing(const Data& audio_data) const -> bool;

		/// @brief Sets the 3D listener position and orientation
		auto set_listener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up) -> void;

		/// @brief Gets the length of an audio file in milliseconds
		[[nodiscard]]
		auto get_length(const Data& audio_data) const -> unsigned int;
	} core{ this };

private:
	AudioSystem() = default;
	~AudioSystem() = default;
	
	// Non-copyable, non-movable singleton
	AudioSystem(const AudioSystem&) = delete;
	AudioSystem& operator=(const AudioSystem&) = delete;
	AudioSystem(AudioSystem&&) = delete;
	AudioSystem& operator=(AudioSystem&&) = delete;
	
	static AudioSystem* m_instance;

	[[nodiscard]]
	auto is_loaded(const Data& audio_data) const -> bool;
	
	auto set_3d_channel_position(const Data& audio_data, FMOD::Channel* channel) const -> void;
	auto initialize_reverb() -> void;
	auto debug_event_info(const FMOD::Studio::EventDescription* event_desc) const -> void;

	struct {
		FMOD::Studio::System* studio_system = nullptr;
		FMOD::System* low_level_system = nullptr;
		
		static constexpr unsigned int MAX_AUDIO_CHANNELS = 255;
		FMOD::ChannelGroup* master_group = nullptr;

		static constexpr float DISTANCE_FACTOR = 1.0f;    // Meters
		FMOD_VECTOR listener_position = { 0.0f, 0.0f, -1.0f * DISTANCE_FACTOR };
		FMOD_VECTOR forward = { 0.0f, 0.0f, 1.0f };
		FMOD_VECTOR up = { 0.0f, 1.0f, 0.0f };

		FMOD::Reverb3D* reverb = nullptr;
		FMOD_VECTOR reverb_pos = { 0.0f, 0.0f, 0.0f };
		float reverb_min_dist = 10.0f;
		float reverb_max_dist = 50.0f;
		
		bool muted = false;

		// Resource caches using string views as keys for efficiency
		std::map<std::string, FMOD::Sound*> sounds;
		std::map<std::string, FMOD::Channel*> loops_playing;
		std::map<std::string, FMOD::Studio::Bank*> sound_banks;
		std::map<std::string, FMOD::Studio::EventDescription*> event_descriptions;
		std::map<std::string, FMOD::Studio::EventInstance*> event_instances;
	} m;
};

}
