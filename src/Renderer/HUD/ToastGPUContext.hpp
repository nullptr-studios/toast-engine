/// @file ToastGPUContext.hpp
/// @brief GPU Context for Ultralight rendering in Toast Engine
/// @author dario
/// @date 12/02/2026.

#pragma once

#include <Ultralight/platform/GPUDriver.h>
#include <Ultralight/platform/Config.h>
#include <memory>

typedef struct GLFWwindow GLFWwindow;

#define ENABLE_OFFSCREEN_GL 0

namespace toast::hud {

// Forward declaration
class ToastGPUDriver;

///
/// @class ToastGPUContext
/// @brief OpenGL GPU context for Ultralight rendering
///
/// Manages the OpenGL context and GPU driver for Ultralight Views.
/// This class handles context switching for multi-window scenarios
/// and provides MSAA support when enabled.
///
class ToastGPUContext {
public:
    ///
    /// @brief Construct a new GPU context
    /// @param window The main GLFW window
    /// @param enable_msaa Whether to enable MSAA rendering
    ///
    ToastGPUContext(GLFWwindow* window, bool enable_msaa = false);
    
    ~ToastGPUContext();
    
    /// @brief Get the GPU driver implementation
    ToastGPUDriver* driver() const { return driver_.get(); }
    
    /// @brief Get the face winding order for rendering
    ultralight::FaceWinding face_winding() const { 
        return ultralight::FaceWinding::CounterClockwise; 
    }
    
    /// @brief Called before drawing operations
    void BeginDrawing();
    
    /// @brief Called after drawing operations
    void EndDrawing();
    
    /// @brief Check if MSAA is enabled
    bool msaa_enabled() const { return msaa_enabled_; }
    
    /// @brief Get the main window
    GLFWwindow* window() const { return window_; }
    
    /// @brief Set the currently active window for FBO operations
    /// @note FBOs are not shared across GL contexts, so we track the active window
    void set_active_window(GLFWwindow* win) { active_window_ = win; }
    
    /// @brief Get the currently active window
    GLFWwindow* active_window() const { return active_window_; }
    
private:
    std::unique_ptr<ToastGPUDriver> driver_;
    GLFWwindow* window_ = nullptr;
    GLFWwindow* active_window_ = nullptr;
    bool msaa_enabled_ = false;
};

} // namespace toast::hud

