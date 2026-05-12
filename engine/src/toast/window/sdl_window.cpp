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

	m.sdl_window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
	    SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow
	);

	TOAST_ASSERT(m.sdl_window, "Window", "SDL Window couldn't be created, check vulkan context creation?");
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

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP: {
				int action = 0;    // 0 = released, 1 = pressed, 2 = repeated
				if (event.key.down) {
					action = event.key.repeat ? 2 : 1;
				} else {
					action = 0;
				}
				event::send<event::WindowKey>(event.key.key, event.key.scancode, action, event.key.mod);
				break;
			}

			case SDL_EVENT_TEXT_INPUT: {
				if (event.text.text && event.text.text[0]) {
					event::send<event::WindowChar>(event.text.text[0]);
				}
				break;
			}

			case SDL_EVENT_MOUSE_MOTION: {
				event::send<event::WindowMousePosition>(event.motion.x, event.motion.y);
				break;
			}

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: {
				int action = event.button.down ? 1 : 0;
				int mods = SDL_GetModState();
				event::send<event::WindowMouseButton>(event.button.button, action, mods);
				break;
			}

			case SDL_EVENT_MOUSE_WHEEL: {
				event::send<event::WindowMouseScroll>(event.wheel.x, event.wheel.y);
				break;
			}

			case SDL_EVENT_DROP_FILE:
			case SDL_EVENT_DROP_TEXT: {
				// dropped filename
				std::vector<std::string> files;
				if (event.drop.data) {
					files.emplace_back(event.drop.data);
				}
				if (!files.empty()) {
					event::send<event::WindowDrop>(std::move(files));
				}
				break;
			}

			case SDL_EVENT_WINDOW_RESIZED: {
				int w = event.window.data1;
				int h = event.window.data2;
				event::send<event::WindowResize>(w, h);
				break;
			}

				// TODO: GAMEPAD EVENTS

			default: break;
		}
	}
}

void SDLWindow::swapFramebuffers() { }
}
