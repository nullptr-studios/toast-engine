/// @file ToastGPUContext.cpp
/// @brief GPU Context implementation for Ultralight rendering in Toast Engine
/// @author dario
/// @date 12/02/2026.

#include "ToastGPUContext.hpp"

#include "ToastGPUDriver.hpp"

#include <SDL3/SDL_video.h>
#include <Toast/Log.hpp>

namespace toast::hud {

ToastGPUContext::ToastGPUContext(SDL_Window* window, bool enable_msaa) : window_(window), active_window_(window), msaa_enabled_(enable_msaa) {
	// Ensure GL context is current before creating driver (which loads shaders)
	if (window_) {
		SDL_GL_MakeCurrent(window_, SDL_GL_GetCurrentContext());
	}

	driver_ = std::make_unique<ToastGPUDriver>(this);

	TOAST_TRACE("ToastGPUContext initialized (MSAA: {})", enable_msaa ? "enabled" : "disabled");
}

ToastGPUContext::~ToastGPUContext() {
	TOAST_TRACE("ToastGPUContext destroyed");
}

void ToastGPUContext::BeginDrawing() {
	// Ensure we're on the correct GL context
	if (active_window_) {
		SDL_GL_MakeCurrent(active_window_, SDL_GL_GetCurrentContext());
	}
}

void ToastGPUContext::EndDrawing() {
	// Nothing to do here for now
}

}    // namespace toast::hud
