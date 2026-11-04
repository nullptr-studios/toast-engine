/**
 * @file startup_splash.hpp
 * @author dario
 * @date 28 Oct 2026
 */

#pragma once

#include <SDL3/SDL.h>
#include <cstddef>
#include <span>
#include <string>

namespace toast {

/// Software drawn window shown during the first shader compile
class StartupSplash {
public:
	StartupSplash() = default;
	~StartupSplash();

	StartupSplash(const StartupSplash&) = delete;
	auto operator=(const StartupSplash&) -> StartupSplash& = delete;

	/// Opens on the first call with something compiling
	void update(size_t finished, size_t total, std::span<const std::string> compiling);

	[[nodiscard]]
	auto closeRequested() const -> bool {
		return m_close_requested;
	}

private:
	void draw(size_t finished, size_t total, std::span<const std::string> compiling) const;

	SDL_Window* m_window = nullptr;
	SDL_Renderer* m_renderer = nullptr;
	bool m_close_requested = false;
	bool m_video_ref = false;
};

}
