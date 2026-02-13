/// @file HUDLayer.cpp
/// @brief HUD rendering layer implementation using Ultralight
/// @author dario
/// @date 12/02/2026.

#include <Toast/Renderer/HUD/HUDLayer.hpp>
#include <Toast/Renderer/Framebuffer.hpp>
#include "ToastGPUContext.hpp"
#include "ToastGPUDriver.hpp"

#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>
#include <Toast/Resources/ToastFileSystem.hpp>
#include <Toast/Renderer/HUD/ToastLogger.hpp>
#include <Toast/Renderer/HUD/ToastFontLoader.hpp>

#include <Ultralight/Ultralight.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>

#include <GLFW/glfw3.h>
#include <glad/gl.h>

namespace renderer::HUD {

// ============================================================================
// ViewListener for page load notifications
// ============================================================================

class ToastViewListener : public ultralight::ViewListener {
public:
    void OnChangeTitle(ultralight::View* caller, const ultralight::String& title) override {
        TOAST_TRACE("[View] Title changed: {}", std::string(title.utf8().data()));
    }
    
    void OnChangeURL(ultralight::View* caller, const ultralight::String& url) override {
        TOAST_TRACE("[View] URL changed: {}", std::string(url.utf8().data()));
    }
    
    void OnChangeCursor(ultralight::View* caller, ultralight::Cursor cursor) override {
        // Could change system cursor here
    }
    
    void OnAddConsoleMessage(ultralight::View* caller,
                             const ultralight::ConsoleMessage& msg) override {
        std::string message_str = msg.message().utf8().data();
        TOAST_TRACE("[JS Console] {}", message_str);
    }
};

class ToastLoadListener : public ultralight::LoadListener {
public:
    void OnBeginLoading(ultralight::View* caller,
                        uint64_t frame_id,
                        bool is_main_frame,
                        const ultralight::String& url) override {
        if (is_main_frame) {
            TOAST_TRACE("[Load] Begin loading: {}", url.utf8().data());
        }
    }
    
    void OnFinishLoading(ultralight::View* caller,
                         uint64_t frame_id,
                         bool is_main_frame,
                         const ultralight::String& url) override {
        if (is_main_frame) {
            TOAST_TRACE("[Load] Finished loading: {}", url.utf8().data());
        }
    }
    
    void OnFailLoading(ultralight::View* caller,
                       uint64_t frame_id,
                       bool is_main_frame,
                       const ultralight::String& url,
                       const ultralight::String& description,
                       const ultralight::String& error_domain,
                       int error_code) override {
        if (is_main_frame) {
            TOAST_ERROR("[Load] Failed to load: {} - {} ({}:{})", 
                       url.utf8().data(), 
                       description.utf8().data(),
                       error_domain.utf8().data(),
                       error_code);
        }
    }
    
    void OnDOMReady(ultralight::View* caller,
                    uint64_t frame_id,
                    bool is_main_frame,
                    const ultralight::String& url) override {
        if (is_main_frame) {
            TOAST_TRACE("[Load] DOM ready: {}", url.utf8().data());
        }
    }
};

static std::unique_ptr<ToastViewListener> g_view_listener;
static std::unique_ptr<ToastLoadListener> g_load_listener;


// ============================================================================
// HUDLayer Implementation
// ============================================================================

HUDLayer::HUDLayer(GLFWwindow* window, uint32_t width, uint32_t height, bool enable_msaa)
    : ILayer("HUDLayer")
    , window_(window)
    , width_(width)
    , height_(height)
    , msaa_enabled_(enable_msaa) {
    TOAST_TRACE("HUDLayer created ({}x{}, MSAA: {})", width, height, enable_msaa);
}

HUDLayer::~HUDLayer() {
    OnDetach();
}

void HUDLayer::InitPlatform() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    // Initialize platform handlers if not already done

    // Configure Ultralight
    ultralight::Config config;
    
    // Set paths - these should be relative to the executable or absolute paths
    // The resources folder should contain: cacert.pem, icudt67l.dat
    config.resource_path_prefix = "UI/Ultralight/resources/";
    // config.cache_path = "./cache/";
    
    config.face_winding = ultralight::FaceWinding::CounterClockwise;
    config.force_repaint = false;
    config.animation_timer_delay = 1.0 / 60.0;
    config.scroll_timer_delay = 1.0 / 60.0;
    config.recycle_delay = 4.0;
    config.memory_cache_size = 64 * 1024 * 1024; // 64MB
    config.page_cache_size = 0;
    config.override_ram_size = 0;
    config.min_large_heap_size = 32 * 1024 * 1024; // 32MB
    config.min_small_heap_size = 1 * 1024 * 1024;  // 1MB
    config.num_renderer_threads = 0; // Use main thread

    // Set platform handlers
    ultralight::Platform::instance().set_config(config);
    ultralight::Platform::instance().set_file_system(&ToastFileSystem::Get());
    ultralight::Platform::instance().set_logger(&ToastLogger::Get());
    ultralight::Platform::instance().set_font_loader(&ToastFontLoader::Get());
    
    TOAST_TRACE("Ultralight platform initialized");
    TOAST_TRACE("Resource path: UI/Ultralight/resources/");
    TOAST_TRACE("Make sure icudt67l.dat and cacert.pem exist in the resources folder!");
}

void HUDLayer::CreateGPUContext() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    gpu_context_ = std::make_unique<toast::hud::ToastGPUContext>(window_, msaa_enabled_);
    
    // Set the GPU driver for Ultralight
    ultralight::Platform::instance().set_gpu_driver(gpu_context_->driver());
    
    TOAST_TRACE("GPU context created for HUD rendering");
}

void HUDLayer::OnAttach() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    TOAST_TRACE("HUDLayer::OnAttach - Initializing Ultralight...");

    // Ensure OpenGL context is current before any GL operations
    if (window_) {
        glfwMakeContextCurrent(window_);
    } else {
        TOAST_ERROR("HUDLayer::OnAttach - No window provided!");
        return;
    }

    // Initialize platform first (FileSystem, Logger, Config)
    InitPlatform();
    
    // Create GPU context and driver BEFORE creating the renderer
    CreateGPUContext();
    
    // Verify GPU driver was set
    if (!gpu_context_ || !gpu_context_->driver()) {
        TOAST_ERROR("HUDLayer::OnAttach - GPU context/driver not initialized!");
        return;
    }

    TOAST_TRACE("Creating Ultralight Renderer...");
    
    // Check if required resources exist before creating renderer
    if (!ToastFileSystem::Get().FileExists("UI/Ultralight/resources/icudt67l.dat")) {
        TOAST_ERROR("CRITICAL: icudt67l.dat not found in UI/Ultralight/resources/ folder!");
        return;
    }
    
    // Verify cacert.pem exists (optional but recommended)
    if (!ToastFileSystem::Get().FileExists("UI/Ultralight/resources/cacert.pem")) {
        TOAST_WARN("cacert.pem not found in UI/Ultralight/resources/ folder - HTTPS may not work correctly");
    }
    
    TOAST_TRACE("Resources verified, creating renderer...");
    
    // Create the Ultralight renderer
    try {
        renderer_ = ultralight::Renderer::Create();
        
        if (!renderer_) {
            TOAST_ERROR("Renderer::Create() returned nullptr!");
            return;
        }
    } catch (const std::exception& e) {
        TOAST_ERROR("Exception creating Ultralight renderer: {}", e.what());
        return;
    } catch (...) {
        TOAST_ERROR("Unknown exception creating Ultralight renderer!");
        return;
    }
    
    TOAST_TRACE("Ultralight Renderer created successfully");

    // Create view configuration
    ultralight::ViewConfig view_config;
    view_config.is_accelerated = true;  // Use GPU acceleration
    view_config.is_transparent = true;  // Allow transparency
    view_config.initial_device_scale = 1.0;
    view_config.initial_focus = true;
    view_config.enable_images = true;
    view_config.enable_javascript = true;

    TOAST_TRACE("Creating Ultralight View ({}x{})...", width_, height_);
    
    // Create the first view and register it
    auto first_view = CreateView(width_, height_, view_config);
    if (!first_view) {
        TOAST_ERROR("Failed to create Ultralight view!");
        return;
    }
    
    // Set up listeners for debugging
    if (!g_view_listener) {
        g_view_listener = std::make_unique<ToastViewListener>();
    }
    if (!g_load_listener) {
        g_load_listener = std::make_unique<ToastLoadListener>();
    }
    first_view->set_view_listener(g_view_listener.get());
    first_view->set_load_listener(g_load_listener.get());

    // Set the active window for the GPU context
    gpu_context_->set_active_window(window_);
    
    // Create the output framebuffer for the HUD
    CreateFramebuffer();
    // Reusable read FBO for blits
    glGenFramebuffers(1, &read_fbo_);

    TOAST_INFO("HUDLayer attached successfully");
}

void HUDLayer::OnDetach() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    // Cleanup framebuffer
    framebuffer_.reset();
    if (read_fbo_) {
        glDeleteFramebuffers(1, &read_fbo_);
        read_fbo_ = 0;
    }
    
    views_.clear();
    renderer_ = nullptr;

    TOAST_INFO("HUDLayer detached");
}

void HUDLayer::OnTick() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    if (!renderer_) return;

    // Update Ultralight (processes JavaScript, animations, etc.)
    renderer_->Update();
}

void HUDLayer::OnRender() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    if (!renderer_ || !gpu_context_ || views_.empty() || !framebuffer_) return;

    // Begin drawing
    gpu_context_->BeginDrawing();

    // Render all views (this updates textures and command lists)
    renderer_->Render();

    // Execute the GPU command list (renders to Ultralight's internal render targets)
    gpu_context_->driver()->DrawCommandList();

    // End drawing
    gpu_context_->EndDrawing();
    
    // Use currently bound draw framebuffer (do not bind/allocate a new one here)
    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

    // Blit each view's render target onto framebuffer (in insertion order)
    for (const auto& v : views_) {
        if (!v) continue;
        ultralight::RenderTarget target = v->render_target();
        if (target.is_empty || target.texture_id == 0) continue;

        GLuint tex_id = gpu_context_->driver()->GetTextureGLResolved(target.texture_id);
        if (tex_id == 0) continue;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo_);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_id, 0);

        GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            static bool error_logged = false;
            if (!error_logged) {
                TOAST_ERROR("[HUD] Read framebuffer incomplete: 0x{:X}", status);
                error_logged = true;
            }
            continue;
        }

        glBlitFramebuffer(0, 0, target.width, target.height,
                          0, target.height, target.width, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
}

void HUDLayer::LoadURL(const std::string& url) {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    if (views_.empty()) {
        TOAST_ERROR("Cannot load URL - view not initialized");
        return;
    }

    views_.front()->LoadURL(ultralight::String(url.c_str()));
    TOAST_INFO("Loading URL: {}", url);
}

void HUDLayer::LoadHTML(const std::string& html, const std::string& base_url) {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    if (views_.empty()) {
        TOAST_ERROR("Cannot load HTML - view not initialized");
        return;
    }

    views_.front()->LoadHTML(ultralight::String(html.c_str()), 
                    ultralight::String(base_url.c_str()));
    TOAST_INFO("Loaded HTML content");
}

void HUDLayer::Resize(uint32_t width, uint32_t height) {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    if (width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    
    // Resize the Ultralight view
    for (auto& v : views_) {
        if (v) v->Resize(width, height);
    }
    
    // Resize the framebuffer
    if (framebuffer_) {
        framebuffer_->Resize(width, height);
    }
    
    TOAST_INFO("HUD resized to {}x{}", width, height);
}

// ============================================================================
// Framebuffer Management
// ============================================================================

void HUDLayer::CreateFramebuffer() {
    PROFILE_ZONE_C(tracy::Color::Cyan);
    
    Framebuffer::Specs specs;
    specs.width = static_cast<int>(width_);
    specs.height = static_cast<int>(height_);
    specs.multisample = false; // We handle MSAA in Ultralight separately
    
    framebuffer_ = std::make_unique<Framebuffer>(specs);
    framebuffer_->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    framebuffer_->Build();
    
    TOAST_INFO("HUD framebuffer created ({}x{})", width_, height_);
}

uint32_t HUDLayer::GetUltralightTextureGL() const {
    if (views_.empty() || !gpu_context_) return 0;
    
    ultralight::RenderTarget target = views_.front()->render_target();
    if (target.is_empty || target.texture_id == 0) return 0;
    
    return gpu_context_->driver()->GetTextureGLResolved(target.texture_id);
}

// ============================================================================
// Input Handling
// ============================================================================

void HUDLayer::OnMouseMove(int x, int y) {
    if (views_.empty()) return;

    ultralight::MouseEvent evt;
    evt.type = ultralight::MouseEvent::kType_MouseMoved;
    evt.x = x;
    evt.y = y;
    evt.button = ultralight::MouseEvent::kButton_None;
    views_.front()->FireMouseEvent(evt);
}

void HUDLayer::OnMouseButton(int button, int action, int mods) {
    if (views_.empty()) return;

    ultralight::MouseEvent evt;
    evt.type = (action == GLFW_PRESS) ? 
        ultralight::MouseEvent::kType_MouseDown : 
        ultralight::MouseEvent::kType_MouseUp;

    // Get current mouse position
    double xpos, ypos;
    glfwGetCursorPos(window_, &xpos, &ypos);
    evt.x = static_cast<int>(xpos);
    evt.y = static_cast<int>(ypos);

    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            evt.button = ultralight::MouseEvent::kButton_Left;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            evt.button = ultralight::MouseEvent::kButton_Middle;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            evt.button = ultralight::MouseEvent::kButton_Right;
            break;
        default:
            evt.button = ultralight::MouseEvent::kButton_None;
            break;
    }

    views_.front()->FireMouseEvent(evt);
}

void HUDLayer::OnMouseScroll(double xoffset, double yoffset) {
    if (views_.empty()) return;

    ultralight::ScrollEvent evt;
    evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
    evt.delta_x = static_cast<int>(xoffset * 32);
    evt.delta_y = static_cast<int>(yoffset * 32);
    views_.front()->FireScrollEvent(evt);
}

// Helper function to convert GLFW key to Ultralight key
static int GLFWKeyToUltralightKey(int key) {
    switch (key) {
        case GLFW_KEY_SPACE: return ultralight::KeyCodes::GK_SPACE;
        case GLFW_KEY_APOSTROPHE: return ultralight::KeyCodes::GK_OEM_7;
        case GLFW_KEY_COMMA: return ultralight::KeyCodes::GK_OEM_COMMA;
        case GLFW_KEY_MINUS: return ultralight::KeyCodes::GK_OEM_MINUS;
        case GLFW_KEY_PERIOD: return ultralight::KeyCodes::GK_OEM_PERIOD;
        case GLFW_KEY_SLASH: return ultralight::KeyCodes::GK_OEM_2;
        case GLFW_KEY_0: return ultralight::KeyCodes::GK_0;
        case GLFW_KEY_1: return ultralight::KeyCodes::GK_1;
        case GLFW_KEY_2: return ultralight::KeyCodes::GK_2;
        case GLFW_KEY_3: return ultralight::KeyCodes::GK_3;
        case GLFW_KEY_4: return ultralight::KeyCodes::GK_4;
        case GLFW_KEY_5: return ultralight::KeyCodes::GK_5;
        case GLFW_KEY_6: return ultralight::KeyCodes::GK_6;
        case GLFW_KEY_7: return ultralight::KeyCodes::GK_7;
        case GLFW_KEY_8: return ultralight::KeyCodes::GK_8;
        case GLFW_KEY_9: return ultralight::KeyCodes::GK_9;
        case GLFW_KEY_SEMICOLON: return ultralight::KeyCodes::GK_OEM_1;
        case GLFW_KEY_EQUAL: return ultralight::KeyCodes::GK_OEM_PLUS;
        case GLFW_KEY_A: return ultralight::KeyCodes::GK_A;
        case GLFW_KEY_B: return ultralight::KeyCodes::GK_B;
        case GLFW_KEY_C: return ultralight::KeyCodes::GK_C;
        case GLFW_KEY_D: return ultralight::KeyCodes::GK_D;
        case GLFW_KEY_E: return ultralight::KeyCodes::GK_E;
        case GLFW_KEY_F: return ultralight::KeyCodes::GK_F;
        case GLFW_KEY_G: return ultralight::KeyCodes::GK_G;
        case GLFW_KEY_H: return ultralight::KeyCodes::GK_H;
        case GLFW_KEY_I: return ultralight::KeyCodes::GK_I;
        case GLFW_KEY_J: return ultralight::KeyCodes::GK_J;
        case GLFW_KEY_K: return ultralight::KeyCodes::GK_K;
        case GLFW_KEY_L: return ultralight::KeyCodes::GK_L;
        case GLFW_KEY_M: return ultralight::KeyCodes::GK_M;
        case GLFW_KEY_N: return ultralight::KeyCodes::GK_N;
        case GLFW_KEY_O: return ultralight::KeyCodes::GK_O;
        case GLFW_KEY_P: return ultralight::KeyCodes::GK_P;
        case GLFW_KEY_Q: return ultralight::KeyCodes::GK_Q;
        case GLFW_KEY_R: return ultralight::KeyCodes::GK_R;
        case GLFW_KEY_S: return ultralight::KeyCodes::GK_S;
        case GLFW_KEY_T: return ultralight::KeyCodes::GK_T;
        case GLFW_KEY_U: return ultralight::KeyCodes::GK_U;
        case GLFW_KEY_V: return ultralight::KeyCodes::GK_V;
        case GLFW_KEY_W: return ultralight::KeyCodes::GK_W;
        case GLFW_KEY_X: return ultralight::KeyCodes::GK_X;
        case GLFW_KEY_Y: return ultralight::KeyCodes::GK_Y;
        case GLFW_KEY_Z: return ultralight::KeyCodes::GK_Z;
        case GLFW_KEY_LEFT_BRACKET: return ultralight::KeyCodes::GK_OEM_4;
        case GLFW_KEY_BACKSLASH: return ultralight::KeyCodes::GK_OEM_5;
        case GLFW_KEY_RIGHT_BRACKET: return ultralight::KeyCodes::GK_OEM_6;
        case GLFW_KEY_GRAVE_ACCENT: return ultralight::KeyCodes::GK_OEM_3;
        case GLFW_KEY_ESCAPE: return ultralight::KeyCodes::GK_ESCAPE;
        case GLFW_KEY_ENTER: return ultralight::KeyCodes::GK_RETURN;
        case GLFW_KEY_TAB: return ultralight::KeyCodes::GK_TAB;
        case GLFW_KEY_BACKSPACE: return ultralight::KeyCodes::GK_BACK;
        case GLFW_KEY_INSERT: return ultralight::KeyCodes::GK_INSERT;
        case GLFW_KEY_DELETE: return ultralight::KeyCodes::GK_DELETE;
        case GLFW_KEY_RIGHT: return ultralight::KeyCodes::GK_RIGHT;
        case GLFW_KEY_LEFT: return ultralight::KeyCodes::GK_LEFT;
        case GLFW_KEY_DOWN: return ultralight::KeyCodes::GK_DOWN;
        case GLFW_KEY_UP: return ultralight::KeyCodes::GK_UP;
        case GLFW_KEY_PAGE_UP: return ultralight::KeyCodes::GK_PRIOR;
        case GLFW_KEY_PAGE_DOWN: return ultralight::KeyCodes::GK_NEXT;
        case GLFW_KEY_HOME: return ultralight::KeyCodes::GK_HOME;
        case GLFW_KEY_END: return ultralight::KeyCodes::GK_END;
        case GLFW_KEY_CAPS_LOCK: return ultralight::KeyCodes::GK_CAPITAL;
        case GLFW_KEY_SCROLL_LOCK: return ultralight::KeyCodes::GK_SCROLL;
        case GLFW_KEY_NUM_LOCK: return ultralight::KeyCodes::GK_NUMLOCK;
        case GLFW_KEY_PRINT_SCREEN: return ultralight::KeyCodes::GK_SNAPSHOT;
        case GLFW_KEY_PAUSE: return ultralight::KeyCodes::GK_PAUSE;
        case GLFW_KEY_F1: return ultralight::KeyCodes::GK_F1;
        case GLFW_KEY_F2: return ultralight::KeyCodes::GK_F2;
        case GLFW_KEY_F3: return ultralight::KeyCodes::GK_F3;
        case GLFW_KEY_F4: return ultralight::KeyCodes::GK_F4;
        case GLFW_KEY_F5: return ultralight::KeyCodes::GK_F5;
        case GLFW_KEY_F6: return ultralight::KeyCodes::GK_F6;
        case GLFW_KEY_F7: return ultralight::KeyCodes::GK_F7;
        case GLFW_KEY_F8: return ultralight::KeyCodes::GK_F8;
        case GLFW_KEY_F9: return ultralight::KeyCodes::GK_F9;
        case GLFW_KEY_F10: return ultralight::KeyCodes::GK_F10;
        case GLFW_KEY_F11: return ultralight::KeyCodes::GK_F11;
        case GLFW_KEY_F12: return ultralight::KeyCodes::GK_F12;
        case GLFW_KEY_LEFT_SHIFT: return ultralight::KeyCodes::GK_SHIFT;
        case GLFW_KEY_LEFT_CONTROL: return ultralight::KeyCodes::GK_CONTROL;
        case GLFW_KEY_LEFT_ALT: return ultralight::KeyCodes::GK_MENU;
        case GLFW_KEY_LEFT_SUPER: return ultralight::KeyCodes::GK_LWIN;
        case GLFW_KEY_RIGHT_SHIFT: return ultralight::KeyCodes::GK_SHIFT;
        case GLFW_KEY_RIGHT_CONTROL: return ultralight::KeyCodes::GK_CONTROL;
        case GLFW_KEY_RIGHT_ALT: return ultralight::KeyCodes::GK_MENU;
        case GLFW_KEY_RIGHT_SUPER: return ultralight::KeyCodes::GK_RWIN;
        default: return ultralight::KeyCodes::GK_UNKNOWN;
    }
}

void HUDLayer::OnKey(int key, int scancode, int action, int mods) {
    if (views_.empty()) return;

    ultralight::KeyEvent evt;
    evt.type = (action == GLFW_PRESS || action == GLFW_REPEAT) ? 
        ultralight::KeyEvent::kType_RawKeyDown : 
        ultralight::KeyEvent::kType_KeyUp;
    
    evt.virtual_key_code = GLFWKeyToUltralightKey(key);
    evt.native_key_code = scancode;
    
    // Set modifiers
    evt.modifiers = 0;
    if (mods & GLFW_MOD_ALT)
        evt.modifiers |= ultralight::KeyEvent::kMod_AltKey;
    if (mods & GLFW_MOD_CONTROL)
        evt.modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
    if (mods & GLFW_MOD_SHIFT)
        evt.modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
    if (mods & GLFW_MOD_SUPER)
        evt.modifiers |= ultralight::KeyEvent::kMod_MetaKey;

    // Get key identifier
    ultralight::GetKeyIdentifierFromVirtualKeyCode(evt.virtual_key_code, evt.key_identifier);

    views_.front()->FireKeyEvent(evt);

    // Also fire a char event for key down if it's a printable character
    if (action == GLFW_PRESS && evt.virtual_key_code >= 32 && evt.virtual_key_code < 127) {
        ultralight::KeyEvent charEvt;
        charEvt.type = ultralight::KeyEvent::kType_Char;
        charEvt.text = ultralight::String(std::string(1, static_cast<char>(evt.virtual_key_code)).c_str());
        charEvt.unmodified_text = charEvt.text;
        views_.front()->FireKeyEvent(charEvt);
    }
}

void HUDLayer::OnChar(unsigned int codepoint) {
    if (views_.empty()) return;

    ultralight::KeyEvent evt;
    evt.type = ultralight::KeyEvent::kType_Char;
    
    // Convert codepoint to UTF-8 string
    char utf8[5] = {0};
    if (codepoint < 0x80) {
        utf8[0] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    
    evt.text = ultralight::String(utf8);
    evt.unmodified_text = evt.text;
    views_.front()->FireKeyEvent(evt);
}

ultralight::RefPtr<ultralight::View> HUDLayer::CreateView(uint32_t width, uint32_t height, ultralight::ViewConfig config) {
    // Default to accelerated + transparent if caller didn't set
    config.is_accelerated = true;
    config.is_transparent = true;
    config.initial_device_scale = (config.initial_device_scale == 0.0) ? 1.0 : config.initial_device_scale;
    config.initial_focus = true;
    config.enable_images = true;
    config.enable_javascript = true;

    if (!renderer_) {
        TOAST_ERROR("Cannot create view: renderer not initialized");
        return nullptr;
    }

    auto v = renderer_->CreateView(width, height, config, nullptr);
    if (v) {
        views_.push_back(v);
    }
    return v;
}

} // namespace renderer::HUD
