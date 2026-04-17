#include "Toast/Window/Window.hpp"

#include "Toast/Log.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Window/WindowEvents.hpp"

// clang-format off
#include <glad/gl.h>
#include <SDL3/SDL.h>
// clang-format on

#ifdef TOAST_EDITOR
#include <backends/imgui_impl_sdl3.h>
#endif

namespace {
unsigned DecodeUTF8Codepoint(const char* text, int& consumed) {
	const unsigned char c0 = static_cast<unsigned char>(text[0]);
	if (c0 == 0) {
		consumed = 0;
		return 0;
	}
	if ((c0 & 0x80u) == 0) {
		consumed = 1;
		return c0;
	}
	if ((c0 & 0xE0u) == 0xC0u) {
		consumed = 2;
		return ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(text[1]) & 0x3Fu);
	}
	if ((c0 & 0xF0u) == 0xE0u) {
		consumed = 3;
		return ((c0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(text[2]) & 0x3Fu);
	}
	consumed = 4;
	return ((c0 & 0x07u) << 18) | ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(text[2]) & 0x3Fu) << 6) |
	       (static_cast<unsigned char>(text[3]) & 0x3Fu);
}
}

namespace toast {

void Window::SetShowMouseCursor(bool show) {
	if (show) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
}

Window* Window::m_instance = nullptr;

Window::Window(unsigned width, unsigned height, std::string_view name) {
	PROFILE_ZONE_N("Window Construction");

	if (m_instance != nullptr) {
		throw ToastException("Trying to create window but it already exists");
	}
	m_instance = this;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		throw WindowException(-1, SDL_GetError());
	}

	m_properties.width = width;
	m_properties.height = height;
	m_properties.name = name;

	TOAST_INFO("Creating window {0} ({1}, {2})", m_properties.name, m_properties.width, m_properties.height);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	m_sdlWindow = SDL_CreateWindow(
	    name.data(), static_cast<int>(width), static_cast<int>(height), SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
	);
	if (!m_sdlWindow) {
		throw WindowException(-1, SDL_GetError());
	}

	m_glContext = SDL_GL_CreateContext(m_sdlWindow);
	if (!m_glContext) {
		throw WindowException(-1, SDL_GetError());
	}

	SDL_GL_MakeCurrent(m_sdlWindow, m_glContext);
	SDL_GL_SetSwapInterval(0);

	m_listener.Subscribe<event::WindowClose>([&](event::WindowClose*) -> bool {
		m_shouldClose = true;
		return true;
	});
}

Window::~Window() {
	TOAST_INFO("Destroying window");
	if (m_glContext) {
		SDL_GL_DestroyContext(m_glContext);
		m_glContext = nullptr;
	}
	if (m_sdlWindow) {
		SDL_DestroyWindow(m_sdlWindow);
		m_sdlWindow = nullptr;
	}
	SDL_Quit();
}

bool Window::ShouldClose() const {
	return m_shouldClose;
}

glm::uvec2 Window::GetFramebufferSize() const {
	int w = 0;
	int h = 0;
	SDL_GetWindowSizeInPixels(m_sdlWindow, &w, &h);
	return { static_cast<unsigned>(w), static_cast<unsigned>(h) };
}

std::pair<float, float> Window::GetDisplayScale() const {
	const float scale = SDL_GetWindowDisplayScale(m_sdlWindow);
	return { scale, scale };
}

double Window::GetTime() {
	return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0;
}

double Window::GetRefreshFrameTime() {
	return m_refreshFrameTime;
}

void Window::SwapBuffers() {
	PROFILE_ZONE_C(0xFF0000);
	SDL_GL_SwapWindow(m_sdlWindow);
}

void Window::PollEventsOnly() {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
#ifdef TOAST_EDITOR
#endif
#ifdef TOAST_EDITOR
		ImGui_ImplSDL3_ProcessEvent(&ev);
#endif
		switch (ev.type) {
			case SDL_EVENT_QUIT: m_shouldClose = true; break;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				if (ev.window.windowID == SDL_GetWindowID(m_sdlWindow)) {
					m_shouldClose = true;
				}
				break;
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP: {
				const int action = (ev.type == SDL_EVENT_KEY_UP) ? event::WINDOW_INPUT_RELEASED
				                                                 : (ev.key.repeat ? event::WINDOW_INPUT_REPEATED : event::WINDOW_INPUT_PRESSED);
				event::WindowKey::Callback(static_cast<int>(ev.key.key), static_cast<int>(ev.key.scancode), action, static_cast<int>(SDL_GetModState()));
				break;
			}
			case SDL_EVENT_TEXT_INPUT: {
				const char* text = ev.text.text;
				int i = 0;
				while (text[i] != '\0') {
					int consumed = 0;
					const unsigned cp = DecodeUTF8Codepoint(text + i, consumed);
					if (consumed <= 0) {
						break;
					}
					event::WindowChar::Callback(cp);
					i += consumed;
				}
				break;
			}
			case SDL_EVENT_MOUSE_MOTION: event::WindowMousePosition::Callback(ev.motion.x, ev.motion.y); break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP: {
				const int action = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? event::WINDOW_INPUT_PRESSED : event::WINDOW_INPUT_RELEASED;
				event::WindowMouseButton::Callback(static_cast<int>(ev.button.button), action, static_cast<int>(SDL_GetModState()));
				break;
			}
			case SDL_EVENT_MOUSE_WHEEL: event::WindowMouseScroll::Callback(ev.wheel.x, ev.wheel.y); break;
			case SDL_EVENT_GAMEPAD_ADDED:
				event::WindowInputDevice::Callback(static_cast<int>(ev.gdevice.which), event::WINDOW_INPUT_DEVICE_CONNECTED);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				event::WindowInputDevice::Callback(static_cast<int>(ev.gdevice.which), event::WINDOW_INPUT_DEVICE_DISCONNECTED);
				break;
			case SDL_EVENT_DROP_FILE: {
				std::vector<std::string> files;
				files.emplace_back(ev.drop.data ? ev.drop.data : "");
				if (ev.drop.data) {
					SDL_free(const_cast<char*>(ev.drop.data));
				}
				event::WindowDrop::Callback(std::move(files));
				break;
			}
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				if (ev.window.windowID == SDL_GetWindowID(m_sdlWindow)) {
					event::WindowResize::Callback(ev.window.data1, ev.window.data2);
				}
				break;
			default: break;
		}
	}
}

void Window::WaitEventsTimeout(double seconds) {
	const Sint64 timeout_ms = static_cast<Sint64>(seconds * 1000.0);
	SDL_Event event;
	SDL_WaitEventTimeout(&event, static_cast<int>(timeout_ms));
}

void Window::SetDisplayMode(DisplayMode modeScreen) {
	if (modeScreen == m_currentDisplayMode) {
		return;
	}

	if (modeScreen == DisplayMode::WINDOWED) {
		TOAST_INFO("Switching to WINDOWED mode");
		SDL_SetWindowFullscreen(m_sdlWindow, false);
		SDL_SetWindowPosition(m_sdlWindow, m_windowedPos.x, m_windowedPos.y);
		SDL_SetWindowSize(m_sdlWindow, static_cast<int>(m_windowedSize.x), static_cast<int>(m_windowedSize.y));
	} else if (modeScreen == DisplayMode::FULLSCREEN) {
		TOAST_INFO("Switching to FULLSCREEN mode");
		if (m_currentDisplayMode == DisplayMode::WINDOWED) {
			SDL_GetWindowPosition(m_sdlWindow, &m_windowedPos.x, &m_windowedPos.y);
			int w = 0;
			int h = 0;
			SDL_GetWindowSize(m_sdlWindow, &w, &h);
			m_windowedSize = { static_cast<unsigned>(w), static_cast<unsigned>(h) };
		}
		SDL_SetWindowFullscreen(m_sdlWindow, true);
	}

	m_currentDisplayMode = modeScreen;
}

DisplayMode Window::GetDisplayMode() const {
	return m_currentDisplayMode;
}

std::vector<glm::uvec2> Window::GetMonitorSupportedSizes() {
	std::vector<glm::uvec2> sizes;
	const SDL_DisplayID display_id = SDL_GetPrimaryDisplay();
	int count = 0;
	const SDL_DisplayMode* const* modes = SDL_GetFullscreenDisplayModes(display_id, &count);
	if (modes) {
		sizes.reserve(count);
		for (int i = 0; i < count; ++i) {
			sizes.emplace_back(static_cast<unsigned>(modes[i]->w), static_cast<unsigned>(modes[i]->h));
		}
		SDL_free(const_cast<SDL_DisplayMode**>(modes));
	}
	return sizes;
}

void Window::SetResolution(glm::uvec2 res) const {
	SDL_SetWindowSize(m_sdlWindow, static_cast<int>(res.x), static_cast<int>(res.y));
}

void Window::SetVSync(bool vsync) {
	SDL_GL_SetSwapInterval(vsync ? 1 : 0);
	m_vsync = vsync;
}

bool Window::IsMinimized() const {
	auto s = GetFramebufferSize();
	return s.x == 0 || s.y == 0;
}

#pragma region Error_Handling

void Window::ErrorCallback(int error, const char* description) {
	throw WindowException(error, description);
}

WindowException::WindowException(int error, const char* description) : error(error), description(description) {
	std::ostringstream oss;
	oss << error << ": " << description;
	message = oss.str();
	TOAST_ERROR("SDL Error {0}: {1}", error, description);
}

const char* WindowException::what() const noexcept {
	return message.c_str();
}

#pragma endregion

}
