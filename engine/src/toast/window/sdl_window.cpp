#include "sdl_window.hpp"

#include <SDL3/SDL_vulkan.h>
#include <tracy/Tracy.hpp>

namespace toast {

SDLWindow::SDLWindow(const char* title, unsigned width, unsigned height, uint64_t flags) {
	ZoneScoped;
	TOAST_ASSERT(SDL_Init(SDL_INIT_VIDEO) == true, "Window", "SDL cannot be initialized");

	if ((flags & SDL_WINDOW_VULKAN) != 0) {
		const char* video_driver = SDL_GetCurrentVideoDriver();
		TOAST_INFO("SDLWindow", "SDL video driver: {}", video_driver ? video_driver : "(null)");
	}

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
	    SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | flags), SDL_DestroyWindow
	);

	if (!m.sdl_window) {
		TOAST_ERROR("SDLWindow", "SDL_CreateWindow failed: {}", SDL_GetError());
	}
	TOAST_ASSERT(m.sdl_window, "Window", "SDL Window couldn't be created: {}", SDL_GetError());

	// Let the UI know the starting dpi ratio
	event::send<event::WindowDisplayScale>(SDL_GetWindowDisplayScale(m.sdl_window.get()));
}

auto SDLWindow::shouldClose() const -> bool {
	return m.should_close;
}

auto SDLWindow::nativeHandle() const -> void* {
	return m.sdl_window.get();
}

void SDLWindow::pollEvents() {
	ZoneScoped;

	for (SDL_Event event; SDL_PollEvent(&event);) {
		ZoneScopedN("SDLWindow::event");

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
				// SDL hands over UTF-8
				// decode and send one event per codepoint so text input works beyond ASCII
				const char* text = event.text.text;
				size_t i = 0;
				while (text != nullptr && text[i] != '\0') {
					const auto byte = static_cast<unsigned char>(text[i]);
					unsigned codepoint = 0;
					size_t length = 1;
					if (byte < 0x80) {
						codepoint = byte;
					} else if ((byte & 0xE0) == 0xC0) {
						codepoint = byte & 0x1Fu;
						length = 2;
					} else if ((byte & 0xF0) == 0xE0) {
						codepoint = byte & 0x0Fu;
						length = 3;
					} else if ((byte & 0xF8) == 0xF0) {
						codepoint = byte & 0x07u;
						length = 4;
					} else {
						i++;    // stray continuation byte
						continue;
					}

					size_t j = 1;
					for (; j < length && (static_cast<unsigned char>(text[i + j]) & 0xC0) == 0x80; j++) {
						codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3Fu);
					}
					if (j == length) {
						event::send<event::WindowChar>(codepoint);
					}
					i += j;
				}
				break;
			}

			case SDL_EVENT_MOUSE_MOTION: {
				// Mouse comes in window coordinates
				// the renderer and UI work in pixels
				const float density = SDL_GetWindowPixelDensity(m.sdl_window.get());
				event::send<event::WindowMousePosition>(event.motion.x * density, event.motion.y * density);
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
				// data1/data2 are window coordinates
				// the swapchain wants pixels
				int w = 0;
				int h = 0;
				SDL_GetWindowSizeInPixels(m.sdl_window.get(), &w, &h);
				event::send<event::WindowResize>(w, h);
				break;
			}

			case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
				event::send<event::WindowDisplayScale>(SDL_GetWindowDisplayScale(m.sdl_window.get()));
				break;
			}

			default: break;
		}
	}
}

void SDLWindow::swapFramebuffers() {
	ZoneScoped;
}

}
