#include "startup_splash.hpp"

#include <algorithm>
#include <format>
#include <toast/log.hpp>

namespace toast {

namespace {

constexpr int k_width = 640;
constexpr int k_height = 220;
/// SDL_RenderDebugText glyphs are 8 pixels
constexpr float k_scale = 2.0f;
constexpr size_t k_listed_shaders = 3;

}

StartupSplash::~StartupSplash() {
	if (m_renderer != nullptr) {
		SDL_DestroyRenderer(m_renderer);
	}
	if (m_window != nullptr) {
		SDL_DestroyWindow(m_window);
	}
	if (m_video_ref) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
}

void StartupSplash::update(size_t finished, size_t total, std::span<const std::string> compiling) {
	if (m_window == nullptr) {
		if (compiling.empty()) {
			return;
		}
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
			TOAST_WARN("SDLWindow", "Startup splash needs SDL video: {}", SDL_GetError());
			return;
		}
		m_video_ref = true;
		m_window = SDL_CreateWindow("Toast Engine", k_width, k_height, 0);
		if (m_window == nullptr) {
			TOAST_WARN("SDLWindow", "Startup splash window failed: {}", SDL_GetError());
			return;
		}
		m_renderer = SDL_CreateRenderer(m_window, "software");
		if (m_renderer == nullptr) {
			TOAST_WARN("SDLWindow", "Startup splash renderer failed: {}", SDL_GetError());
		}
	}

	for (SDL_Event event; SDL_PollEvent(&event);) {
		if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
			m_close_requested = true;
		}
	}

	draw(finished, total, compiling);
}

void StartupSplash::draw(size_t finished, size_t total, std::span<const std::string> compiling) const {
	if (m_renderer == nullptr) {
		return;
	}

	SDL_SetRenderScale(m_renderer, k_scale, k_scale);
	SDL_SetRenderDrawColor(m_renderer, 22, 22, 26, 255);
	SDL_RenderClear(m_renderer);

	constexpr float margin = 16.0f;
	constexpr float logical_width = static_cast<float>(k_width) / k_scale;

	SDL_SetRenderDrawColor(m_renderer, 160, 160, 170, 255);
	SDL_RenderDebugText(m_renderer, margin, 17.0f, "Compiling shaders...");

	const float fraction = total == 0 ? 1.0f : std::clamp(static_cast<float>(finished) / static_cast<float>(total), 0.0f, 1.0f);
	const SDL_FRect track {margin, 48.0f, logical_width - 2.0f * margin, 10.0f};
	SDL_SetRenderDrawColor(m_renderer, 48, 48, 56, 255);
	SDL_RenderFillRect(m_renderer, &track);
	const SDL_FRect fill {track.x, track.y, track.w * fraction, track.h};
	SDL_SetRenderDrawColor(m_renderer, 255, 176, 64, 255);
	SDL_RenderFillRect(m_renderer, &fill);

	SDL_SetRenderDrawColor(m_renderer, 240, 240, 240, 255);
	SDL_RenderDebugText(m_renderer, margin, 64.0f, std::format("{} / {}", finished, total).c_str());

	SDL_SetRenderDrawColor(m_renderer, 160, 160, 170, 255);
	float y = 80.0f;
	for (size_t i = 0; i < std::min(compiling.size(), k_listed_shaders); ++i) {
		SDL_RenderDebugText(m_renderer, margin, y, compiling[i].c_str());
		y += 10.0f;
	}
	if (compiling.size() > k_listed_shaders) {
		SDL_RenderDebugText(m_renderer, margin, y, std::format("+{} more", compiling.size() - k_listed_shaders).c_str());
	}

	SDL_RenderPresent(m_renderer);
}

}
