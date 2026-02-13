/// @file HUDLayer.hpp
/// @brief HUD rendering layer using Ultralight for web-based UI
/// @author dario
/// @date 12/02/2026.

#pragma once

#include "Toast/Renderer/ILayer.hpp"

#include <Ultralight/Ultralight.h>
#include <memory>
#include <string>

typedef struct GLFWwindow GLFWwindow;
class Framebuffer;

namespace toast::hud {
    class ToastGPUContext;
    class ToastGPUDriver;
}

namespace renderer::HUD {

///
/// @class HUDLayer
/// @brief Rendering layer for HTML/CSS/JS based UI using Ultralight
///
/// This layer manages Ultralight Views for rendering web-based UI elements.
/// It integrates with the Toast Engine's rendering pipeline and handles
/// GPU-accelerated rendering of web content.
///
/// The HUD renders to its own framebuffer which can be retrieved via GetFramebuffer()
/// for compositing by the main renderer.
///
/// @par Usage Example:
/// @code
/// auto hudLayer = std::make_unique<HUDLayer>(window, 1920, 1080);
/// hudLayer->OnAttach();
/// hudLayer->LoadURL("file:///assets/ui/main_menu.html");
/// // In game loop:
/// hudLayer->OnTick();
/// hudLayer->OnRender();
/// // Get the framebuffer texture for compositing
/// GLuint hudTexture = hudLayer->GetFramebuffer()->GetColorTexture(0);
/// @endcode
///
class HUDLayer : public ILayer {
public:
    ///
    /// @brief Construct a new HUD Layer
    /// @param window The GLFW window to render to
    /// @param width Initial viewport width
    /// @param height Initial viewport height
    /// @param enable_msaa Whether to enable MSAA for UI rendering
    ///
    HUDLayer(GLFWwindow* window, uint32_t width, uint32_t height, bool enable_msaa = false);
    
    ~HUDLayer() override;

    /// @brief Initialize the Ultralight renderer and create views
    void OnAttach() override;
    
    /// @brief Cleanup Ultralight resources
    void OnDetach() override;
    
    /// @brief Update Ultralight logic (JavaScript execution, animations)
    void OnTick() override;
    
    /// @brief Render the UI to the HUD framebuffer
    void OnRender() override;

    // =========================================================================
    // View Management
    // =========================================================================

    ///
    /// @brief Load a URL into the first view (if any)
    /// @param url The URL to load (can be file:// or http://)
    ///
    void LoadURL(const std::string& url);
    
    ///
    /// @brief Load HTML content directly into the first view (if any)
    /// @param html The HTML content string
    /// @param base_url Base URL for resolving relative paths
    ///
    void LoadHTML(const std::string& html, const std::string& base_url = "");

    ///
    /// @brief Create an additional Ultralight view managed by this HUD layer.
    /// @param width View width
    /// @param height View height
    /// @param config Optional view configuration (defaults to transparent, accelerated)
    /// @return RefPtr to the created view
    ultralight::RefPtr<ultralight::View> CreateView(uint32_t width, uint32_t height, ultralight::ViewConfig config = {});
     
     ///
     /// @brief Resize the UI viewport
     /// @param width New viewport width
     /// @param height New viewport height
    ///
    void Resize(uint32_t width, uint32_t height);
    
    ///
    /// @brief Get the Ultralight View for direct manipulation
    /// @return RefPtr to the Ultralight View
    ///
    ultralight::RefPtr<ultralight::View> GetView() const { return views_.empty() ? nullptr : views_.front(); }
    const std::vector<ultralight::RefPtr<ultralight::View>>& GetViews() const { return views_; }
    
    ///
    /// @brief Get the HUD framebuffer for compositing
    /// @return Pointer to the framebuffer containing the rendered HUD
    ///
    Framebuffer* GetFramebuffer() const { return framebuffer_.get(); }
    
    ///
    /// @brief Get the raw Ultralight render target texture ID (for debugging)
    /// @return OpenGL texture ID of Ultralight's internal render target, or 0 if not available
    ///
    uint32_t GetUltralightTextureGL() const;

    // =========================================================================
    // Input Handling
    // =========================================================================

    /// @brief Handle mouse move events
    void OnMouseMove(int x, int y);
    
    /// @brief Handle mouse button events
    void OnMouseButton(int button, int action, int mods);
    
    /// @brief Handle mouse scroll events
    void OnMouseScroll(double xoffset, double yoffset);
    
    /// @brief Handle key events
    void OnKey(int key, int scancode, int action, int mods);
    
    /// @brief Handle character input events
    void OnChar(unsigned int codepoint);

private:
    /// @brief Initialize Ultralight platform handlers
    void InitPlatform();
    
    /// @brief Create the GPU context and driver
    void CreateGPUContext();
    
    /// @brief Create the output framebuffer
    void CreateFramebuffer();

    GLFWwindow* window_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool msaa_enabled_ = false;

    std::unique_ptr<toast::hud::ToastGPUContext> gpu_context_;
    ultralight::RefPtr<ultralight::Renderer> renderer_;
    std::vector<ultralight::RefPtr<ultralight::View>> views_;
    
    // Output framebuffer for the HUD
    std::unique_ptr<Framebuffer> framebuffer_;
    unsigned read_fbo_ = 0;
};

} // namespace renderer::HUD
