/**
 * @file sdl_window.hpp
 * @author Xein
 * @date 5/7/2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "base_window.hpp"
#include "toast/events/listener.hpp"
#include "toast/log.hpp"
#include "window_events.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <string_view>

namespace toast {

class SDLWindow : public BaseWindow {
public:
	SDLWindow(std::string_view title, unsigned width = 800, unsigned height = 600, int flags = 0) {
		TOAST_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0, BaseWindow, "SDL cannot be initialized");

		// subscribe window to events
		m.event_listener.subscribe<event::ExitApplication>([this] { m.should_close = true; });
		m.event_listener.subscribe<event::WindowClose>([this](const event::WindowClose& e) {
			// check for the window ID first
			if (e.window_id == SDL_GetWindowID(m.sdl_window.get())) {
				m.should_close = true;
				return true;
			}

			return false;
		});

		m = {
		  .sdl_window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
		      SDL_CreateWindow(title.data(), width, height, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow
		  ),
		};

		TOAST_ASSERT(m.sdl_window, BaseWindow, "SDL Window couldn't be created");
	}

	// SDLWindow(std::string_view title, unsigned pos_x, unsigned pos_y, unsigned width, unsigned height, unsigned flags);
	~SDLWindow() override = default;

	// Lifetime
	[[nodiscard]]
	auto shouldClose() const -> bool override;

	void pollEvents() override;

private:
	struct {
		// do not confuse SDL_Window with SDLWindow, they are not the same
		std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> sdl_window = {nullptr, SDL_DestroyWindow};
		event::Listener event_listener;
		bool should_close = false;
		WindowType type = WindowType::sdl;
	} m;
};

}
