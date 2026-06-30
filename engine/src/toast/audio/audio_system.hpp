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

namespace audio {

class AudioSystem {
public:
	AudioSystem() noexcept;
	~AudioSystem() noexcept;

	[[nodiscard]]
	static auto get() noexcept -> AudioSystem&;

	void tick() const noexcept;

	void generateIntermediates(const std::filesystem::path& path);
	
	[[nodiscard]]
	auto loadBank(assets::AssetHandle<assets::AudioBank> bank) const -> std::pair<FMOD_STUDIO_BANK*, std::vector<std::string>>;
	void unloadBank(FMOD_STUDIO_BANK*) const;
	
	void updateListenerAttributes(
	    int id, glm::vec3 pos, glm::vec3 velocity, glm::vec3 forward, glm::vec3 up, std::optional<glm::vec3> attenuation_override
	);
	void updateListenerWeight(int id, float weight);
	void setListenerPosition(int index, glm::vec3 pos);
	[[nodiscard]] auto listenerPositions() const -> const std::vector<glm::vec3>&;
	
	void playEvent(std::string_view guidStr);
	void stopEvent(std::string_view guidStr, bool allowFadeout);
	void pauseEvent(std::string_view guidStr, bool value);
	void setParameter(std::string_view guidStr, std::string_view name, float value);
	void setParameter(std::string_view guidStr, std::string_view name, bool value);
	void setVolume(std::string_view guidStr, float volume);
	void setPitch(std::string_view guidStr, float pitch);
	[[nodiscard]] auto isEventPlaying(std::string_view guidStr) -> bool;
	
	[[nodiscard]] auto playEvent3D(std::string_view guidStr) -> uint64_t;
	void stopEvent3D(uint64_t instanceId, bool allowFadeout);
	void set3DAttributes(uint64_t instanceId, const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& forward, const glm::vec3& up);
	void set3DMinMaxDistance(uint64_t instanceId, float minDistance, float maxDistance);
	void pauseEvent(uint64_t instanceId, bool value);
	void setParameter(uint64_t instanceId, std::string_view name, float value);
	void setParameter(uint64_t instanceId, std::string_view name, bool value);
	void setVolume(uint64_t instanceId, float volume);
	void setPitch(uint64_t instanceId, float pitch);
	[[nodiscard]] auto isEventPlaying(uint64_t instanceId) -> bool;
	void set3DOverrideAttenuation(uint64_t instanceId, float minDistance, float maxDistance);

private:
	static inline AudioSystem* instance = nullptr;
	FMOD_STUDIO_SYSTEM* m_system;
	FMOD_SYSTEM* m_core_system;
	
	std::unordered_map<std::string, FMOD_STUDIO_EVENTINSTANCE*> m_active_instances;
	auto getOrCreateInstance(std::string_view guidStr) -> FMOD_STUDIO_EVENTINSTANCE*;

	uint64_t m_next_instance_id = 1; // 0 = null
	std::unordered_map<uint64_t, FMOD_STUDIO_EVENTINSTANCE*> m_instances_3d;

	std::vector<glm::vec3> m_listener_positions;
};

}
