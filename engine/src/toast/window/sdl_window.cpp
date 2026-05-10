#include "sdl_window.hpp"

namespace toast {

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
			default:
		}
	}
}

}
