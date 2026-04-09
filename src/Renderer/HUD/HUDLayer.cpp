/// @file HUDLayer.cpp
/// @brief HUD rendering layer implementation using Ultralight
/// @author dario
/// @date 12/02/2026.

#include "Toast/Renderer/IRendererBase.hpp"
#include "Toast/Ui/FontHandler.hpp"
#include "Toast/Ui/Logger.hpp"
#include "Toast/Window/Window.hpp"
#include "Toast/Window/WindowEvents.hpp"
#include "ToastGPUContext.hpp"
#include "ToastGPUDriver.hpp"

#include <AppCore/Platform.h>
#include <JavaScriptCore/JSRetainPtr.h>
#include <SDL3/SDL.h>
#include <Toast/Log.hpp>
#include <Toast/Profiler.hpp>
#include <Toast/Renderer/Framebuffer.hpp>
#include <Toast/Renderer/HUD/HUDLayer.hpp>
#include <Toast/Renderer/HUD/ShowHUDLayer.h>
#include <Toast/Resources/ToastFileSystem.hpp>
#include <Ultralight/Ultralight.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/platform/Platform.h>
#include <algorithm>

namespace renderer::HUD {

// Initialize static instance pointer
HUDLayer* renderer::HUD::HUDLayer::s_Instance = nullptr;

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

	void OnAddConsoleMessage(ultralight::View* caller, const ultralight::ConsoleMessage& msg) override {
		std::string message_str = msg.message().utf8().data();
		TOAST_TRACE("[JS Console] {}", message_str);

		// If the page signals that GameUI is ready, flush any queued HUD scripts immediately
		try {
			if (message_str.find("[GameUI] ready") != std::string::npos) {
				if (auto hud = renderer::HUD::HUDLayer::Get()) {
					hud->FlushPendingScriptsNow();
					TOAST_TRACE("[HUD] Flushed pending scripts due to GameUI ready message");
				}
			}
		} catch (...) {
			// Swallow any exception from logging to avoid crashing the console handler
		}
	}
};

class ToastLoadListener : public ultralight::LoadListener {
public:
	void OnBeginLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame) {
			TOAST_TRACE("[Load] Begin loading: {}", url.utf8().data());
		}
	}

	void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame) {
			TOAST_TRACE("[Load] Finished loading: {}", url.utf8().data());
		}
	}

	void OnFailLoading(
	    ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url, const ultralight::String& description,
	    const ultralight::String& error_domain, int error_code
	) override {
		if (is_main_frame) {
			TOAST_ERROR("[Load] Failed to load: {} - {} ({}:{})", url.utf8().data(), description.utf8().data(), error_domain.utf8().data(), error_code);
		}
	}

	void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame) {
			TOAST_TRACE("[Load] DOM ready: {}", url.utf8().data());
		}
	}
};

static std::unique_ptr<ToastViewListener> g_view_listener;
static std::unique_ptr<ToastLoadListener> g_load_listener;

// Internal per-HUDLayer load listener that sets dom_ready_ and flushes pending scripts.
class HUDLayerLoadListener : public ultralight::LoadListener {
public:
	HUDLayerLoadListener(renderer::HUD::HUDLayer* owner, ultralight::LoadListener* forward = nullptr) : owner_(owner), forward_(forward) { }

	void OnBeginLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame) {
			TOAST_TRACE("[Load] Begin loading (HUDLayer): {}", url.utf8().data());
		}
		if (forward_) {
			forward_->OnBeginLoading(caller, frame_id, is_main_frame, url);
		}
	}

	void OnFinishLoading(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (is_main_frame) {
			TOAST_TRACE("[Load] Finished loading (HUDLayer): {}", url.utf8().data());
		}
		if (forward_) {
			forward_->OnFinishLoading(caller, frame_id, is_main_frame, url);
		}
	}

	void OnFailLoading(
	    ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url, const ultralight::String& description,
	    const ultralight::String& error_domain, int error_code
	) override {
		if (is_main_frame) {
			TOAST_ERROR(
			    "[Load] Failed to load (HUDLayer): {} - {} ({}:{})", url.utf8().data(), description.utf8().data(), error_domain.utf8().data(), error_code
			);
		}
		if (forward_) {
			forward_->OnFailLoading(caller, frame_id, is_main_frame, url, description, error_domain, error_code);
		}
	}

	void OnDOMReady(ultralight::View* caller, uint64_t frame_id, bool is_main_frame, const ultralight::String& url) override {
		if (!is_main_frame) {
			return;
		}
		TOAST_TRACE("[Load] DOM ready (HUDLayer): {}", url.utf8().data());
		if (owner_) {
			owner_->OnDOMReady();
		}
		if (forward_) {
			forward_->OnDOMReady(caller, frame_id, is_main_frame, url);
		}
	}

private:
	renderer::HUD::HUDLayer* owner_ = nullptr;
	ultralight::LoadListener* forward_ = nullptr;    // Not owned
};

HUDLayer::HUDLayer(SDL_Window* window, uint32_t width, uint32_t height, bool enable_msaa)
    : ILayer("HUDLayer"),
      window_(window),
      width_(width),
      height_(height),
      msaa_enabled_(enable_msaa) {
	TOAST_TRACE("HUDLayer created ({}x{}, MSAA: {})", width, height, enable_msaa);

	// Set singleton instance to this
	s_Instance = this;

	listener.Subscribe<event::WindowMousePosition>([this](event::WindowMousePosition* e) -> bool {
		if (!IsInputEnabled()) {
			return false;
		}
		OnMouseMove(static_cast<int>(e->x), static_cast<int>(e->y));
		return false;
	});

	listener.Subscribe<event::WindowMouseButton>([this](event::WindowMouseButton* e) -> bool {
		if (!IsInputEnabled()) {
			return false;
		}
		OnMouseButton(e->button, e->action, e->mods);
		return false;
	});

	listener.Subscribe<event::WindowMouseScroll>([this](event::WindowMouseScroll* e) -> bool {
		if (!IsInputEnabled()) {
			return false;
		}
		OnMouseScroll(e->x, e->y);
		return false;
	});

	listener.Subscribe<event::WindowKey>([this](event::WindowKey* e) -> bool {
		if (!IsInputEnabled()) {
			return false;
		}
		OnKey(e->key, e->scancode, e->action, e->mods);
		return false;
	});

	listener.Subscribe<event::WindowChar>([this](event::WindowChar* e) -> bool {
		if (!IsInputEnabled()) {
			return false;
		}
		OnChar(e->key);
		return false;
	});

	listener.Subscribe<ShowHUDLayerEvent>([this](ShowHUDLayerEvent* e) -> bool {
		if (e->s) {
			TOAST_TRACE("Received ShowHUDLayerEvent: show=true");
			this->Enable();
		} else {
			TOAST_TRACE("Received ShowHUDLayerEvent: show=false");
			this->Disable();
		}
		return true;
	});
}

HUDLayer::~HUDLayer() {
	// Clear singleton if this is the current instance
	if (s_Instance == this) {
		s_Instance = nullptr;
	}

	// OnDetach();
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
		SDL_GL_MakeCurrent(window_, toast::Window::GetInstance()->GetGLContext());
	} else {
		TOAST_ERROR("HUDLayer::OnAttach - No window provided!");
		return;
	}

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

	// Set up shared listeners
	if (!g_view_listener) {
		g_view_listener = std::make_unique<ToastViewListener>();
	}
	if (!g_load_listener) {
		g_load_listener = std::make_unique<ToastLoadListener>();
	}
	load_listener_ = std::make_unique<HUDLayerLoadListener>(this, g_load_listener.get());

	// If a URL was requested before init via LoadURL(), create a view and load it now
	if (!pending_url_.empty()) {
		// Read the monitor DPI scale so Ultralight's CSS coordinate space equals logical pixels
		if (window_) {
			device_scale_ = SDL_GetWindowDisplayScale(window_);
		}

		ultralight::ViewConfig view_config;
		view_config.is_accelerated = true;
		view_config.is_transparent = true;
		view_config.initial_device_scale = device_scale_;
		view_config.initial_focus = true;
		view_config.enable_images = true;
		view_config.enable_javascript = true;

		auto first_view = CreateView(width_, height_, view_config);
		if (first_view) {
			TOAST_TRACE("HUDLayer::OnAttach - loading pending URL: {}", pending_url_);
			first_view->LoadURL(ultralight::String(pending_url_.c_str()));
		}
		pending_url_.clear();
	}

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
	view_sort_orders_.clear();
	view_composite_flags_.clear();
	renderer_ = nullptr;

	TOAST_INFO("HUDLayer detached");
}

void HUDLayer::OnTick() {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	if (!renderer_ || !active_) {
		return;
	}

	// Update Ultralight (processes JavaScript, animations, etc.)
	renderer_->Update();
}

void HUDLayer::OnRender() {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	if (!renderer_ || !gpu_context_ || views_.empty() || !framebuffer_ || !active_) {
		return;
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_STENCIL_TEST);

	renderer_->RefreshDisplay(0);

	// Begin drawing
	gpu_context_->BeginDrawing();

	// Render all views (updates textures and command lists)
	renderer_->Render();

	// Execute GPU commands
	gpu_context_->driver()->DrawCommandList();

	// End drawing
	gpu_context_->EndDrawing();

	GLint prev_fbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

	// Bind HUD framebuffer
	framebuffer_->bind();

	glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
	glScissor(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Enable alpha compositing
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for (const auto& v : views_) {
		if (!v) {
			continue;
		}

		auto composite_it = view_composite_flags_.find(v.get());
		if (composite_it != view_composite_flags_.end() && !composite_it->second) {
			continue;
		}

		ultralight::RenderTarget target = v->render_target();
		if (target.is_empty || target.texture_id == 0) {
			continue;
		}

		GLuint tex_id = gpu_context_->driver()->GetTextureGLResolved(target.texture_id);
		if (tex_id == 0) {
			continue;
		}

		// Bind texture to slot 0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex_id);

		// Draw fullscreen quad (renderer expects texture in slot 0)
		IRendererBase::GetInstance()->DrawScreenQuad(true);
	}

	glDisable(GL_BLEND);

	// Restore previous framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
}

void HUDLayer::LoadURL(const std::string& url) {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	if (views_.empty()) {
		// View not created yet — remember URL and it will be loaded in OnAttach
		TOAST_TRACE("HUDLayer::LoadURL - view not initialized yet, deferring URL: {}", url);
		pending_url_ = url;
		return;
	}

	// Reset dom_ready state and clear any pending scripts when loading a new page
	dom_ready_ = false;
	pending_scripts_.clear();

	views_.front()->LoadURL(ultralight::String(url.c_str()));
	TOAST_INFO("Loading URL: {}", url);
}

// Helper to escape a C++ string for safe injection into a JS single-quoted string literal
static std::string JSEscape(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '\'': out += "\\'"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c; break;
		}
	}
	return out;
}

void HUDLayer::LoadHTML(const std::string& html, const std::string& base_url) {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	if (views_.empty()) {
		TOAST_ERROR("Cannot load HTML - view not initialized");
		return;
	}

	// Reset dom_ready state and clear any pending scripts when loading new content
	dom_ready_ = false;
	pending_scripts_.clear();

	views_.front()->LoadHTML(ultralight::String(html.c_str()), ultralight::String(base_url.c_str()));
	TOAST_INFO("Loaded HTML content");
}

// Evaluate a script now if DOM is ready, otherwise queue it to be run after DOM ready.
void HUDLayer::EvalScriptOrQueue(const std::string& script) {
	if (dom_ready_) {
		if (!views_.empty()) {
			for (const auto& v : views_) {
				if (v) {
					v->EvaluateScript(ultralight::String(script.c_str()));
				}
			}
		}
		return;
	}

	// Queue up to a reasonable limit to avoid unbounded growth
	if (pending_scripts_.size() > 256) {
		// drop oldest
		pending_scripts_.erase(pending_scripts_.begin());
	}
	pending_scripts_.push_back(script);
}

// Called by LoadListener when DOM is ready
void HUDLayer::OnDOMReady() {
	// Mark DOM ready
	dom_ready_ = true;
	// Flush pending scripts
	for (const auto& s : pending_scripts_) {
		if (!views_.empty()) {
			std::string wrapped = std::string("(function(){ setTimeout(function(){ ") + s + std::string(" }, 50); })();");
			for (const auto& v : views_) {
				v->EvaluateScript(ultralight::String(wrapped.c_str()));
			}
		}
	}
	pending_scripts_.clear();
}

// Execute any queued scripts immediately (used when the page emits '[GameUI] ready')
void HUDLayer::FlushPendingScriptsNow() {
	if (pending_scripts_.empty()) {
		return;
	}
	if (views_.empty() || !views_.front()) {
		return;
	}

	for (const auto& s : pending_scripts_) {
		views_.front()->EvaluateScript(ultralight::String(s.c_str()));
	}
	pending_scripts_.clear();
}

void HUDLayer::ExecuteJS(const std::string& script) {
	EvalScriptOrQueue(script);
}

// JS wrapper API implementations
[[deprecated("THIS SHOULD NOT BE ON THE ENGINE")]]
void HUDLayer::SetWeaponSlot(int index, const std::string& icon_url, int ammo) {
	ExecuteJS(std::format("setWeaponSlot({}, \"{}\", {})", index, icon_url, ammo));
}

[[deprecated("THIS SHOULD NOT BE ON THE ENGINE")]]
void HUDLayer::SetWeaponAmmo(int index, int ammo) {
	ExecuteJS(std::format("setWeaponAmmo({}, {})", index, ammo));
}

[[deprecated("THIS SHOULD NOT BE ON THE ENGINE")]]
void HUDLayer::SetSelectedWeapon(int index) {
	ExecuteJS(std::format("setSelectedWeapon({})", index));
}

void HUDLayer::Enable() {
	active_ = true;
	TOAST_INFO("HUD enabled");
}

void HUDLayer::Disable() {
	active_ = false;
	TOAST_INFO("HUD disabled");
}

void HUDLayer::Resize(uint32_t width, uint32_t height) {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	if (width == 0 || height == 0) {
		return;
	}
	if (width == width_ && height == height_) {
		return;
	}

	width_ = width;
	height_ = height;

	// Refresh DPI scale — the monitor or window scale may have changed
	if (window_) {
		device_scale_ = SDL_GetWindowDisplayScale(window_);
	}

	// Resize the Ultralight view (physical pixels) and update the device scale
	for (auto& v : views_) {
		if (v) {
			v->Resize(width, height);
			// v->set_device_scale(device_scale_);
		}
	}

	// Resize the framebuffer
	if (framebuffer_) {
		framebuffer_->Resize(width, height);
	}

	TOAST_INFO("HUD resized to {}x{} (device_scale={:.2f})", width, height, device_scale_);
}

// ============================================================================
// Framebuffer Management
// ============================================================================

void HUDLayer::CreateFramebuffer() {
	PROFILE_ZONE_C(tracy::Color::Cyan);

	Framebuffer::Specs specs;
	specs.width = static_cast<int>(width_);
	specs.height = static_cast<int>(height_);
	specs.multisample = false;    // We handle MSAA in Ultralight separately

	framebuffer_ = std::make_unique<Framebuffer>(specs);
	framebuffer_->AddColorAttachment(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	framebuffer_->Build();

	TOAST_INFO("HUD framebuffer created ({}x{})", width_, height_);
}

uint32_t HUDLayer::GetUltralightTextureGL() const {
	if (views_.empty()) {
		return 0;
	}
	return GetViewTextureGL(views_.front());
}

uint32_t HUDLayer::GetViewTextureGL(const ultralight::RefPtr<ultralight::View>& view) const {
	if (!view || !gpu_context_) {
		return 0;
	}

	ultralight::RenderTarget target = view->render_target();
	if (target.is_empty || target.texture_id == 0) {
		return 0;
	}

	return gpu_context_->driver()->GetTextureGLResolved(target.texture_id);
}

// ============================================================================
// Input Handling
// ============================================================================

void HUDLayer::OnMouseMove(int x, int y) {
	if (views_.empty()) {
		return;
	}

	ultralight::MouseEvent evt;
	evt.type = ultralight::MouseEvent::kType_MouseMoved;
	evt.x = x - viewport_offset_x_;
	evt.y = y - viewport_offset_y_;
	evt.button = ultralight::MouseEvent::kButton_None;
	for (auto& v : views_) {
		v->FireMouseEvent(evt);
	}
}

void HUDLayer::OnMouseButton(int button, int action, int mods) {
	if (views_.empty()) {
		return;
	}

	ultralight::MouseEvent evt;
	evt.type = (action == event::WINDOW_INPUT_PRESSED) ? ultralight::MouseEvent::kType_MouseDown : ultralight::MouseEvent::kType_MouseUp;

	// Get current mouse position
	float xpos = 0.0f;
	float ypos = 0.0f;
	SDL_GetMouseState(&xpos, &ypos);
	evt.x = static_cast<int>(xpos) - viewport_offset_x_;
	evt.y = static_cast<int>(ypos) - viewport_offset_y_;

	switch (button) {
		case SDL_BUTTON_LEFT: evt.button = ultralight::MouseEvent::kButton_Left; break;
		case SDL_BUTTON_MIDDLE: evt.button = ultralight::MouseEvent::kButton_Middle; break;
		case SDL_BUTTON_RIGHT: evt.button = ultralight::MouseEvent::kButton_Right; break;
		default: evt.button = ultralight::MouseEvent::kButton_None; break;
	}

	for (auto& v : views_) {
		v->FireMouseEvent(evt);
	}
}

void HUDLayer::OnMouseScroll(double xoffset, double yoffset) {
	if (views_.empty()) {
		return;
	}

	ultralight::ScrollEvent evt;
	evt.type = ultralight::ScrollEvent::kType_ScrollByPixel;
	evt.delta_x = static_cast<int>(xoffset * 32);
	evt.delta_y = static_cast<int>(yoffset * 32);
	for (auto& v : views_) {
		v->FireScrollEvent(evt);
	}
}

// Helper function to convert SDL key to Ultralight key
static int SDLKeyToUltralightKey(int key) {
	if (key >= SDLK_A && key <= SDLK_Z) {
		return ultralight::KeyCodes::GK_A + (key - SDLK_A);
	}
	if (key >= SDLK_0 && key <= SDLK_9) {
		return ultralight::KeyCodes::GK_0 + (key - SDLK_0);
	}
	if (key >= SDLK_F1 && key <= SDLK_F12) {
		return ultralight::KeyCodes::GK_F1 + (key - SDLK_F1);
	}

	switch (key) {
		case SDLK_SPACE: return ultralight::KeyCodes::GK_SPACE;
		case SDLK_APOSTROPHE: return ultralight::KeyCodes::GK_OEM_7;
		case SDLK_COMMA: return ultralight::KeyCodes::GK_OEM_COMMA;
		case SDLK_MINUS: return ultralight::KeyCodes::GK_OEM_MINUS;
		case SDLK_PERIOD: return ultralight::KeyCodes::GK_OEM_PERIOD;
		case SDLK_SLASH: return ultralight::KeyCodes::GK_OEM_2;
		case SDLK_SEMICOLON: return ultralight::KeyCodes::GK_OEM_1;
		case SDLK_EQUALS: return ultralight::KeyCodes::GK_OEM_PLUS;
		case SDLK_LEFTBRACKET: return ultralight::KeyCodes::GK_OEM_4;
		case SDLK_BACKSLASH: return ultralight::KeyCodes::GK_OEM_5;
		case SDLK_RIGHTBRACKET: return ultralight::KeyCodes::GK_OEM_6;
		case SDLK_GRAVE: return ultralight::KeyCodes::GK_OEM_3;
		case SDLK_ESCAPE: return ultralight::KeyCodes::GK_ESCAPE;
		case SDLK_RETURN: return ultralight::KeyCodes::GK_RETURN;
		case SDLK_TAB: return ultralight::KeyCodes::GK_TAB;
		case SDLK_BACKSPACE: return ultralight::KeyCodes::GK_BACK;
		case SDLK_INSERT: return ultralight::KeyCodes::GK_INSERT;
		case SDLK_DELETE: return ultralight::KeyCodes::GK_DELETE;
		case SDLK_RIGHT: return ultralight::KeyCodes::GK_RIGHT;
		case SDLK_LEFT: return ultralight::KeyCodes::GK_LEFT;
		case SDLK_DOWN: return ultralight::KeyCodes::GK_DOWN;
		case SDLK_UP: return ultralight::KeyCodes::GK_UP;
		case SDLK_PAGEUP: return ultralight::KeyCodes::GK_PRIOR;
		case SDLK_PAGEDOWN: return ultralight::KeyCodes::GK_NEXT;
		case SDLK_HOME: return ultralight::KeyCodes::GK_HOME;
		case SDLK_END: return ultralight::KeyCodes::GK_END;
		case SDLK_CAPSLOCK: return ultralight::KeyCodes::GK_CAPITAL;
		case SDLK_SCROLLLOCK: return ultralight::KeyCodes::GK_SCROLL;
		case SDLK_NUMLOCKCLEAR: return ultralight::KeyCodes::GK_NUMLOCK;
		case SDLK_PRINTSCREEN: return ultralight::KeyCodes::GK_SNAPSHOT;
		case SDLK_PAUSE: return ultralight::KeyCodes::GK_PAUSE;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT: return ultralight::KeyCodes::GK_SHIFT;
		case SDLK_LCTRL:
		case SDLK_RCTRL: return ultralight::KeyCodes::GK_CONTROL;
		case SDLK_LALT:
		case SDLK_RALT: return ultralight::KeyCodes::GK_MENU;
		case SDLK_LGUI: return ultralight::KeyCodes::GK_LWIN;
		case SDLK_RGUI: return ultralight::KeyCodes::GK_RWIN;
		default: return ultralight::KeyCodes::GK_UNKNOWN;
	}
}

void HUDLayer::OnKey(int key, int scancode, int action, int mods) {
	if (views_.empty()) {
		return;
	}

	ultralight::KeyEvent evt;
	evt.type = (action == event::WINDOW_INPUT_PRESSED || action == event::WINDOW_INPUT_REPEATED) ? ultralight::KeyEvent::kType_RawKeyDown
	                                                                                             : ultralight::KeyEvent::kType_KeyUp;

	evt.virtual_key_code = SDLKeyToUltralightKey(key);
	evt.native_key_code = scancode;

	// Set modifiers
	evt.modifiers = 0;
	if (mods & SDL_KMOD_ALT) {
		evt.modifiers |= ultralight::KeyEvent::kMod_AltKey;
	}
	if (mods & SDL_KMOD_CTRL) {
		evt.modifiers |= ultralight::KeyEvent::kMod_CtrlKey;
	}
	if (mods & SDL_KMOD_SHIFT) {
		evt.modifiers |= ultralight::KeyEvent::kMod_ShiftKey;
	}
	if (mods & SDL_KMOD_GUI) {
		evt.modifiers |= ultralight::KeyEvent::kMod_MetaKey;
	}

	// Get key identifier
	ultralight::GetKeyIdentifierFromVirtualKeyCode(evt.virtual_key_code, evt.key_identifier);

	for (auto& v : views_) {
		v->FireKeyEvent(evt);
	}
}

void HUDLayer::OnChar(unsigned int codepoint) {
	if (views_.empty()) {
		return;
	}

	ultralight::KeyEvent evt;
	evt.type = ultralight::KeyEvent::kType_Char;

	// Convert codepoint to UTF-8 string
	char utf8[5] = { 0 };
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
	for (auto& v : views_) {
		v->FireKeyEvent(evt);
	}
}

ultralight::RefPtr<ultralight::View> HUDLayer::CreateView(uint32_t width, uint32_t height, ultralight::ViewConfig config, bool composite_to_hud) {
	// Default to accelerated + transparent if caller didn't set
	config.is_accelerated = true;
	config.is_transparent = true;
	config.initial_device_scale = (config.initial_device_scale == 0.0) ? device_scale_ : config.initial_device_scale;
	config.initial_focus = true;
	config.enable_images = true;
	config.enable_javascript = true;

	if (!renderer_) {
		TOAST_ERROR("Cannot create view: renderer not initialized");
		return nullptr;
	}

	auto v = renderer_->CreateView(width, height, config, nullptr);
	if (v) {
		if (g_view_listener) {
			v->set_view_listener(g_view_listener.get());
		}
		if (load_listener_) {
			v->set_load_listener(load_listener_.get());
		}
		views_.push_back(v);
		view_composite_flags_[v.get()] = composite_to_hud;
	}
	return v;
}

void HUDLayer::RemoveView(const ultralight::RefPtr<ultralight::View>& view) {
	auto it = std::find(views_.begin(), views_.end(), view);
	if (it != views_.end()) {
		bool wasFront = (it == views_.begin());
		view_sort_orders_.erase(view.get());
		view_composite_flags_.erase(view.get());
		views_.erase(it);
		// If the front view changed, reset DOM readiness so scripts queue until new front is ready
		if (wasFront) {
			dom_ready_ = false;
		}
	}
}

void HUDLayer::SetViewSortOrder(const ultralight::RefPtr<ultralight::View>& view, int order) {
	if (!view) {
		return;
	}
	view_sort_orders_[view.get()] = order;
	SortViewsByOrder();
}

void HUDLayer::SortViewsByOrder() {
	std::stable_sort(
	    views_.begin(), views_.end(), [this](const ultralight::RefPtr<ultralight::View>& a, const ultralight::RefPtr<ultralight::View>& b) {
		    int oa = 0, ob = 0;
		    auto itA = view_sort_orders_.find(a.get());
		    auto itB = view_sort_orders_.find(b.get());
		    if (itA != view_sort_orders_.end()) {
			    oa = itA->second;
		    }
		    if (itB != view_sort_orders_.end()) {
			    ob = itB->second;
		    }
		    return oa < ob;
	    }
	);
}

}    // namespace renderer::HUD

// ============================================================================
// Blur Implementation (in renderer::HUD namespace via reopening for clarity)
// ============================================================================

namespace renderer::HUD {

static const char* kBlurVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kBlurFragSrc = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uDirection;
uniform vec2 uResolution;

void main() {
    vec2 texelSize = 1.0 / uResolution;
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec4 result = texture(uTexture, vUV) * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * texelSize * float(i) * 2.0;
        result += texture(uTexture, vUV + offset) * weights[i];
        result += texture(uTexture, vUV - offset) * weights[i];
    }
    FragColor = result;
}
)";

static GLuint CompileShader(GLenum type, const char* src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		TOAST_ERROR("[HUD Blur] Shader compile error: {}", log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

}
