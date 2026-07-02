/**
 * @file audio_system.hpp
 * @author Xein
 * @date 29 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "assets.hpp"

#include <filesystem>
#include <fmod/fmod_studio.h>
#include <glm/glm.hpp>
#include <toast/world/box.hpp>

namespace toast {
class AudioListener;
class AudioVolume;
}

namespace audio {

class AudioSystem {
public:
	AudioSystem() noexcept;
	~AudioSystem() noexcept;

	[[nodiscard]]
	static auto get() noexcept -> AudioSystem&;

	void tick() noexcept;

	void generateIntermediates(const std::filesystem::path& path);

	[[nodiscard]]
	auto loadBank(assets::AssetHandle<assets::AudioBank> bank) const -> std::pair<FMOD_STUDIO_BANK*, std::vector<std::string>>;
	void unloadBank(FMOD_STUDIO_BANK*) const;

	void updateListenerAttributes(
	    int id, glm::vec3 pos, glm::vec3 velocity, glm::vec3 forward, glm::vec3 up, std::optional<glm::vec3> attenuation_override
	);
	void updateListenerWeight(int id, float weight);
	void setListenerPosition(int index, glm::vec3 pos);
	[[nodiscard]]
	auto listenerPositions() const -> const std::vector<glm::vec3>&;
	void registerListener(toast::AudioListener& listener);
	void unregisterListener(toast::AudioListener& listener);
	void registerVolume(toast::AudioVolume& volume);
	void unregisterVolume(toast::AudioVolume& volume);

	void playEvent(std::string_view guid_str);
	void stopEvent(std::string_view guid_str, bool allow_fadeout);
	void pauseEvent(std::string_view guid_str, bool value);
	void setParameter(std::string_view guid_str, std::string_view name, float value);
	void setParameter(std::string_view guid_str, std::string_view name, bool value);
	void setVolume(std::string_view guid_str, float volume);
	void setPitch(std::string_view guid_str, float pitch);
	[[nodiscard]]
	auto isEventPlaying(std::string_view guid_str) -> bool;

	// Runtime control for VCA, Bus, and Port assets
	void setVcaVolume(std::string_view guid_str, float volume);
	void setBusVolume(std::string_view guid_str, float volume);
	void setPortVolume(std::string_view guid_str, float volume);

	// Snapshot runtime controls (intensity = 0.0 - 1.0, enable/disable)
	void setSnapshotIntensity(std::string_view guid_str, float intensity);
	void setSnapshotEnabled(std::string_view guid_str, bool enabled);

	[[nodiscard]]
	auto playEvent3D(std::string_view guid_str) -> uint64_t;
	void stopEvent3D(uint64_t instance_id, bool allow_fadeout);
	void set3DAttributes(
	    uint64_t instance_id, const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& fwd, const glm::vec3& up
	);
	void set3DMinMaxDistance(uint64_t instance_id, float min_distance, float max_distance);
	void pauseEvent(uint64_t instance_id, bool value);
	void setParameter(uint64_t instance_id, std::string_view name, float value);
	void setParameter(uint64_t instance_id, std::string_view name, bool value);
	void setVolume(uint64_t instance_id, float volume);
	void setPitch(uint64_t instance_id, float pitch);
	[[nodiscard]]
	auto isEventPlaying(uint64_t instance_id) -> bool;
	void set3DOverrideAttenuation(uint64_t instance_id, float min_distance, float max_distance);

	void keyOffEvent(uint64_t instance_id);
	[[nodiscard]]
	auto getTimelinePosition(uint64_t instance_id) -> int;
	void setTimelinePosition(uint64_t instance_id, int ms);
	void setParameterByID(uint64_t instance_id, FMOD_STUDIO_PARAMETER_ID id, float value);
	[[nodiscard]]
	auto getPlaybackState(uint64_t instance_id) -> FMOD_STUDIO_PLAYBACK_STATE;

	[[nodiscard]]
	auto getRawInstance(uint64_t instance_id) -> FMOD_STUDIO_EVENTINSTANCE*;

private:
	static inline AudioSystem* instance = nullptr;
	FMOD_STUDIO_SYSTEM* m_system;
	FMOD_SYSTEM* m_core_system;

	auto loadBankData(const std::vector<uint8_t>& data) const -> FMOD_STUDIO_BANK*;

	std::unordered_map<std::string, FMOD_STUDIO_EVENTINSTANCE*> m_active_instances;
	auto getOrCreateInstance(std::string_view guid_str) -> FMOD_STUDIO_EVENTINSTANCE*;

	uint64_t m_next_instance_id = 1;    // 0 = null
	std::unordered_map<uint64_t, FMOD_STUDIO_EVENTINSTANCE*> m_instances_3d;

	std::unordered_map<std::string, FMOD_STUDIO_EVENTINSTANCE*> m_snapshot_instances;

	std::vector<glm::vec3> m_listener_positions;
	std::vector<toast::Box<toast::AudioListener>> m_listeners;
	std::mutex m_listeners_mutex;

	std::vector<toast::Box<toast::AudioVolume>> m_volumes;
	std::mutex m_volumes_mutex;
};

}
