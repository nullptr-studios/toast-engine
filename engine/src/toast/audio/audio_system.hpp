/**
 * @file audio_system.hpp
 * @author Xein
 * @date 29 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "toast/log.hpp"
#include "assets.hpp"

#include <filesystem>
#include <fmod/fmod_studio.h>

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

private:
	static inline AudioSystem* instance = nullptr;
	FMOD_STUDIO_SYSTEM* m_system;
	FMOD_SYSTEM* m_core_system;
};

}
