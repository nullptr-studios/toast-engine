#include "sdl_window.hpp"

namespace toast {

SDLWindow::SDLWindow(const char* title, unsigned width, unsigned height, int flags) {
	TOAST_ASSERT(SDL_Init(SDL_INIT_VIDEO) == true, "Window", "SDL cannot be initialized");

	type = WindowType::sdl;

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
	      SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow
	  ),
	};

	// TOAST_ASSERT(m.sdl_window, "Window", "SDL Window couldn't be created");
}

auto SDLWindow::shouldClose() const -> bool {
	return m.should_close;
}

void SDLWindow::pollEvents() {
	for (SDL_Event event; SDL_PollEvent(&event);) {
		if (event.window.windowID != SDL_GetWindowID(m.sdl_window.get())) {
			continue;
		}

		switch (event.type) {
			case SDL_EVENT_QUIT: {
				// This event is for exiting the whole application, usually mapped to Alt+F4 or Cmd+Q
				event::send<event::ExitApplication>();
				break;
			}
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
				// This event is for closing only one window, usually mapped to Ctrl+W or Cmd+W
				event::send<event::WindowClose>(event.window.windowID);
				break;
			}
			default: break;
		}
	}
}

void SDLWindow::swapFramebuffers() { }
}
