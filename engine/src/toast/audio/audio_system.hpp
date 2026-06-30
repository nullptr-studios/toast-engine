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

private:
	static inline AudioSystem* instance = nullptr;
	FMOD_STUDIO_SYSTEM* m_system;
	FMOD_SYSTEM* m_core_system;
};

}
