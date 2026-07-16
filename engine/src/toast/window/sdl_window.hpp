/**
 * @file sdl_window.hpp
 * @author Xein & Dario
 * @date 7 May 2026
 *
 * @brief Wrapper around the SDL library
 */

#pragma once
#include "base_window.hpp"
#include "window_events.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <toast/events/listener.hpp>
#include <toast/log.hpp>

namespace toast {

class SDLWindow : public IBaseWindow {
public:
	SDLWindow(const char* title, unsigned width = 1080, unsigned height = 720, uint64_t flags = 0);

	~SDLWindow() override = default;

	// Lifetime
	[[nodiscard]]
	auto shouldClose() const -> bool override;
	[[nodiscard]]
	auto nativeHandle() const -> void* override;

	void pollEvents() override;
	void swapFramebuffers() override;

private:
	struct {
		// do not confuse SDL_Window with SDLWindow, they are not the same
		std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> sdl_window = {nullptr, SDL_DestroyWindow};
		event::Listener event_listener;
		bool should_close = false;
	} m;
};

}
