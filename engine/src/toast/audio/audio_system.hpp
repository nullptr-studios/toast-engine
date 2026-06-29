/**
 * @file audio_system.hpp
 * @author Xein
 * @date 29 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "toast/log.hpp"

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

private:
	static inline AudioSystem* instance = nullptr;
	FMOD_STUDIO_SYSTEM* m_system;
	FMOD_SYSTEM* m_core_system;
};

}
