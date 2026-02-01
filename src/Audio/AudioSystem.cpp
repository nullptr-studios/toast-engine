/// @file AudioSystem.cpp

#include "AudioSystem.hpp"

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>
#include <fmod_errors.h>

audio::AudioSystem* audio::AudioSystem::m_instance = nullptr;

auto audio::AudioSystem::create() -> std::expected<AudioSystem*, AudioError> {
	if (m_instance != nullptr) {
		TOAST_ERROR("AudioSystem: Attempted to create AudioSystem instance when one already exists!");
		return std::unexpected(AudioError::AlreadyLoaded);
	}

	m_instance = new AudioSystem();
	m_instance->Init();    // Initialize FMOD systems immediately to catch errors early
	return m_instance;
}

auto audio::AudioSystem::get() noexcept -> AudioSystem* {
	if (not m_instance) {
		TOAST_ERROR("AudioSystem: Attempted to get AudioSystem instance before it was created!\n");
		std::abort();
	}
	return m_instance;
}

void audio::AudioSystem::Init() {
	// Create FMOD Studio system first, then get low-level system from it
	// Both systems are needed: Studio for events, Core for raw audio playback
	ERRCHECK(FMOD::Studio::System::create(&m.studio_system));
	ERRCHECK(m.studio_system->getCoreSystem(&m.low_level_system));
	ERRCHECK(m.low_level_system->setSoftwareFormat(AUDIO_SAMPLE_RATE, FMOD_SPEAKERMODE_5POINT1, 0));
	ERRCHECK(m.low_level_system->set3DSettings(1.0, DISTANCE_FACTOR, 0.5f));

	// live-update while on debug
	ERRCHECK(m.studio_system->initialize(MAX_AUDIO_CHANNELS, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, 0));

	ERRCHECK(m.low_level_system->getMasterChannelGroup(&m.master_group));
	initialize_reverb();
}

void audio::AudioSystem::Destroy() const {
	m.low_level_system->close();
	m.studio_system->release();
}

void audio::AudioSystem::Tick() const {
	ERRCHECK(m.studio_system->update());    // also updates the low level system
}

auto audio::AudioSystem::CoreSystem::load(Data& audio_data) -> std::expected<void, AudioError> {
	PROFILE_ZONE;
	if (!owner->is_loaded(audio_data)) {
		TOAST_INFO("[AudioSystem] Loading Sound from file {}", audio_data.GetFilePath());
		FMOD::Sound* sound;
		ERRCHECK(owner->m.low_level_system->createSound(audio_data.GetFilePath(), audio_data.Is3D() ? FMOD_3D : FMOD_2D, 0, &sound));
		ERRCHECK(sound->setMode(audio_data.Loop() ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));
		ERRCHECK(sound->set3DMinMaxDistance(0.5f * owner->DISTANCE_FACTOR, 5000.0f * owner->DISTANCE_FACTOR));
		// Cache sound for later playback to avoid reloading from disk
		owner->m.sounds.insert({ audio_data.GetUniqueID(), sound });
		unsigned int msLength = 0;
		ERRCHECK(owner->m.sounds[audio_data.GetUniqueID()]->getLength(&msLength, FMOD_TIMEUNIT_MS));
		audio_data.SetLengthMS(msLength);
		audio_data.SetLoaded(true);
		return {};
	}

	TOAST_WARN("[AudioSystem] Sound File was already loaded!");
	return std::unexpected(AudioError::AlreadyLoaded);
}

auto audio::AudioSystem::CoreSystem::play(const Data& audio_data) -> std::expected<void, AudioError> {
	if (!owner->is_loaded(audio_data)) {
		TOAST_ERROR("[AudioSystem] Can't play, sound was not loaded yet from {}", audio_data.GetFilePath());
		return std::unexpected(AudioError::NotLoaded);
	}

	TOAST_INFO("[AudioSystem] Playing Sound: {}", audio_data.GetUniqueID());
	FMOD::Channel* channel;
	// Start paused to configure 3D position and volume before playback begins
	ERRCHECK(owner->m.low_level_system->playSound(owner->m.sounds[audio_data.GetUniqueID()], nullptr, true /* start paused */, &channel));

	if (audio_data.Is3D()) {
		owner->set_3d_channel_position(audio_data, channel);
	}

	channel->setVolume(audio_data.GetVolume());

	// Track looping sounds so they can be stopped/updated later
	if (audio_data.Loop()) {
		owner->m.loops_playing.insert({ audio_data.GetUniqueID(), channel });
	}

	ERRCHECK(channel->setReverbProperties(0, audio_data.GetReverbAmount()));

	// start audio playback
	ERRCHECK(channel->setPaused(false));
	return {};
}

auto audio::AudioSystem::CoreSystem::stop(const Data& audio_data) -> std::expected<void, AudioError> {
	if (!is_playing(audio_data)) {
		TOAST_WARN("[AudioSystem] Can't stop a looping sound that's not playing!");
		return std::unexpected(AudioError::NotPlaying);
	}

	TOAST_INFO("[AudioSystem] Stopping sound {}", audio_data.GetUniqueID());
	ERRCHECK(owner->m.loops_playing[audio_data.GetUniqueID()]->stop());
	owner->m.loops_playing.erase(audio_data.GetUniqueID());
	return {};
}

auto audio::AudioSystem::CoreSystem::update_volume(Data& audio_data, float new_volume, unsigned int fade_length) -> std::expected<void, AudioError> {
	if (!is_playing(audio_data)) {
		TOAST_WARN("[AudioSystem] Can't update sound loop volume! (It isn't playing or might not be loaded)");
		return std::unexpected(AudioError::NotPlaying);
	}

	FMOD::Channel* channel = owner->m.loops_playing[audio_data.GetUniqueID()];
	// Use instant volume change for short fades since FMOD's default is 64 samples anyway
	if (fade_length <= 64) {
		ERRCHECK(channel->setVolume(new_volume));
	} else {
		bool fade_up = new_volume > audio_data.GetVolume();
		// get current audio clock time
		unsigned long long parentclock = 0;
		ERRCHECK(channel->getDSPClock(nullptr, &parentclock));

		// Fade up needs immediate volume set + fade to 1.0, fade down just fades to target
		float target_fade_vol = fade_up ? 1.0f : new_volume;

		if (fade_up) {
			ERRCHECK(channel->setVolume(new_volume));
		}

		ERRCHECK(channel->addFadePoint(parentclock, audio_data.GetVolume()));
		ERRCHECK(channel->addFadePoint(parentclock + fade_length, target_fade_vol));
	}

	audio_data.SetVolume(new_volume);
	return {};
}

auto audio::AudioSystem::CoreSystem::update_position(Data& audio_data) -> std::expected<void, AudioError> {
	if (!this->is_playing(audio_data)) {
		TOAST_WARN("[AudioSystem] Can't update sound position!");
		return std::unexpected(AudioError::NotPlaying);
	}

	owner->set_3d_channel_position(audio_data, owner->m.loops_playing[audio_data.GetUniqueID()]);
	return {};
}

auto audio::AudioSystem::CoreSystem::is_playing(const Data& audio_data) const -> bool {
	// Only looping sounds are tracked, one-shots play and forget
	return audio_data.Loop() && owner->m.loops_playing.contains(audio_data.GetUniqueID());
}

auto audio::AudioSystem::CoreSystem::set_listener(glm::vec3 pos, glm::vec3 forward, glm::vec3 up) -> void {
	owner->m.listener_position = { pos.x, pos.y, pos.z };
	owner->m.forward = { forward.x, forward.y, forward.z };
	owner->m.up = { up.x, up.y, up.z };
	ERRCHECK(owner->m.low_level_system->set3DListenerAttributes(0, &owner->m.listener_position, 0, &owner->m.forward, &owner->m.up));
}

auto audio::AudioSystem::CoreSystem::get_length(const Data& audio_data) const -> unsigned int {
	unsigned int length = 0;
	if (owner->m.sounds.contains(audio_data.GetUniqueID())) {
		ERRCHECK(owner->m.sounds[audio_data.GetUniqueID()]->getLength(&length, FMOD_TIMEUNIT_MS));
	}
	return length;
}

auto audio::AudioSystem::load_bank(std::string_view filepath) -> std::expected<void, AudioError> {
	PROFILE_ZONE;
	TOAST_INFO("[AudioSystem] Loading FMOD Studio Sound Bank {}", filepath);
	FMOD::Studio::Bank* bank = nullptr;
	ERRCHECK(m.studio_system->loadBankFile(std::string(filepath).c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &bank));
	m.sound_banks.insert({ std::string(filepath), bank });
	return {};
}

auto audio::AudioSystem::load_event(std::string_view name, std::span<const std::pair<std::string_view, float>> params)
    -> std::expected<void, AudioError> {
	PROFILE_ZONE;
	TOAST_INFO("[AudioSystem] Loading FMOD Studio Event {}", name);
	FMOD::Studio::EventDescription* event_description = nullptr;
	ERRCHECK(m.studio_system->getEvent(std::string(name).c_str(), &event_description));
	// Create an instance of the event
	FMOD::Studio::EventInstance* event_instance = nullptr;
	ERRCHECK(event_description->createInstance(&event_instance));
	// Apply initial parameter values to configure the event before playback
	for (const auto& [param_name, param_value] : params) {
		TOAST_INFO("[AudioSystem] Setting Event Instance Parameter {} to value: {}", param_name, param_value);
		// Set the parameter values of the event instance
		ERRCHECK(event_instance->setParameterByName(std::string(param_name).c_str(), param_value));
	}
	m.event_instances.insert({ std::string(name), event_instance });
	m.event_descriptions.insert({ std::string(name), event_description });
	return {};
}

auto audio::AudioSystem::set_param(std::string_view event_name, std::string_view param_name, float value) -> std::expected<void, AudioError> {
	if (!m.event_instances.contains(std::string(event_name))) {
		TOAST_ERROR("[AudioSystem] Event {} was not in event instance cache, can't set param", event_name);
		return std::unexpected(AudioError::EventNotFound);
	}

	ERRCHECK(m.event_instances[std::string(event_name)]->setParameterByName(std::string(param_name).c_str(), value));
	return {};
}

auto audio::AudioSystem::play(std::string_view event_name) -> std::expected<void, AudioError> {
	if (!m.event_instances.contains(std::string(event_name))) {
		TOAST_ERROR("[AudioSystem] Event {} was not in event instance cache, cannot play", event_name);
		return std::unexpected(AudioError::EventNotFound);
	}

	ERRCHECK(m.event_instances[std::string(event_name)]->start());
	return {};
}

auto audio::AudioSystem::stop(std::string_view event_name) -> std::expected<void, AudioError> {
	if (!m.event_instances.contains(std::string(event_name))) {
		TOAST_ERROR("[AudioSystem] Event {} was not in event instance cache, cannot stop", event_name);
		return std::unexpected(AudioError::EventNotFound);
	}

	ERRCHECK(m.event_instances[std::string(event_name)]->stop(FMOD_STUDIO_STOP_ALLOWFADEOUT));
	return {};
}

auto audio::AudioSystem::set_volume(std::string_view event_name, float volume) -> std::expected<void, AudioError> {
	if (!m.event_instances.contains(std::string(event_name))) {
		TOAST_ERROR("[AudioSystem] Event {} was not in event instance cache, can't set volume", event_name);
		return std::unexpected(AudioError::EventNotFound);
	}

	TOAST_INFO("[AudioSystem] Setting Event Volume");
	ERRCHECK(m.event_instances[std::string(event_name)]->setVolume(volume));
	return {};
}

auto audio::AudioSystem::is_playing(std::string_view event_name) const -> bool {
	if (!m.event_instances.contains(std::string(event_name))) {
		return false;
	}

	FMOD_STUDIO_PLAYBACK_STATE playback_state;
	ERRCHECK(m.event_instances.at(std::string(event_name))->getPlaybackState(&playback_state));
	return playback_state == FMOD_STUDIO_PLAYBACK_PLAYING;
}

auto audio::AudioSystem::mute_all() -> AudioSystem& {
	ERRCHECK(m.master_group->setMute(true));
	m.muted = true;
	return *this;
}

auto audio::AudioSystem::unmute_all() -> AudioSystem& {
	ERRCHECK(m.master_group->setMute(false));
	m.muted = false;
	return *this;
}

auto audio::AudioSystem::is_muted() const noexcept -> bool {
	return m.muted;
}

// Private helper implementations
auto audio::AudioSystem::is_loaded(const Data& audio_data) const -> bool {
	return m.sounds.contains(audio_data.GetUniqueID());
}

auto audio::AudioSystem::set_3d_channel_position(const Data& audio_data, FMOD::Channel* channel) const -> void {
	FMOD_VECTOR position = { audio_data.GetPosition().x * DISTANCE_FACTOR,
		                       audio_data.GetPosition().y * DISTANCE_FACTOR,
		                       audio_data.GetPosition().z * DISTANCE_FACTOR };

	FMOD_VECTOR velocity = { 0.0f, 0.0f, 0.0f };    // TODO: Add doppler eventually
	ERRCHECK(channel->set3DAttributes(&position, &velocity));
}

auto audio::AudioSystem::initialize_reverb() -> void {
	ERRCHECK(m.low_level_system->createReverb3D(&m.reverb));
	FMOD_REVERB_PROPERTIES prop2 = FMOD_PRESET_CONCERTHALL;
	ERRCHECK(m.reverb->setProperties(&prop2));
	ERRCHECK(m.reverb->set3DAttributes(&m.reverb_pos, m.reverb_min_dist, m.reverb_max_dist));
}

// Error checking/debugging function definitions

void ERRCHECK_fn(const FMOD_RESULT result, const char* file, const int line) {
	if (result != FMOD_OK) {
		TOAST_ERROR("FMOD ERROR: {} [Line {}] {} - {}", file, line, static_cast<int>(result), FMOD_ErrorString(result));
	}
}

auto audio::AudioSystem::debug_event_info(const FMOD::Studio::EventDescription* event_desc) const -> void {
	int params;
	bool is_3d, is_oneshot;
	ERRCHECK(event_desc->getParameterDescriptionCount(&params));
	ERRCHECK(event_desc->is3D(&is_3d));
	ERRCHECK(event_desc->isOneshot(&is_oneshot));

	TOAST_INFO(
	    "FMOD EventDescription has {} parameter descriptions, {} 3D, {} oneshot, {} valid.",
	    params,
	    is_3d ? "is" : "isn't",
	    is_oneshot ? "is" : "isn't",
	    event_desc->isValid() ? "is" : "isn't"
	);
}
