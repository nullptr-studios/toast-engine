/// @file ToastGPUContext.cpp
/// @brief GPU Context implementation for Ultralight rendering in Toast Engine
/// @author dario
/// @date 12/02/2026.

#include "ToastGPUContext.hpp"
#include "ToastGPUDriver.hpp"

#include <Toast/Log.hpp>
#include <GLFW/glfw3.h>

namespace toast::hud {

ToastGPUContext::ToastGPUContext(GLFWwindow* window, bool enable_msaa)
    : window_(window)
    , active_window_(window)
    , msaa_enabled_(enable_msaa) {
    
    // Ensure GL context is current before creating driver (which loads shaders)
    if (window_) {
        glfwMakeContextCurrent(window_);
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
        glfwMakeContextCurrent(active_window_);
    }
}

void ToastGPUContext::EndDrawing() {
    // Nothing to do here for now
}

} // namespace toast::hud
