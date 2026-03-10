/// @file HUDLayer.hpp
/// @brief HUD rendering layer using Ultralight for web-based UI
/// @author dario
/// @date 12/02/2026.

#pragma once

#include "Toast/Renderer/ILayer.hpp"

#include <Ultralight/Ultralight.h>
#include <memory>
#include <string>
#include <vector>

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

	// JS wrapper API: call into the HUD's JavaScript from C++/Ultralight
	/// @brief Execute an arbitrary JavaScript string on the first view.
	/// If the DOM is not ready yet the script will be queued and executed once DOMReady fires.
	void ExecuteJS(const std::string& script);

	/// @brief Set a weapon slot's icon URL and ammo count (calls JS: window.setWeaponSlot)
	void SetWeaponSlot(int index, const std::string& icon_url, int ammo);

	/// @brief Set only the ammo count for a weapon slot (calls JS: window.HUD.setSlot)
	void SetWeaponAmmo(int index, int ammo);

	/// @brief Change the currently selected weapon slot (calls JS: window.setSelectedWeapon)
	void SetSelectedWeapon(int index);

	void Enable();     /// Enable the HUD (for previewing in editor)
	void Disable();    /// Disable the HUD (for previewing in editor)

	                   // Singleton accessor: returns the most recently constructed HUDLayer (or nullptr)
	static HUDLayer* Get() {
		return s_Instance;
	}

	///
	/// @brief Create an additional Ultralight view managed by this HUD layer.
	/// @param width View width
	/// @param height View height
	/// @param config Optional view configuration (defaults to transparent, accelerated)
	/// @return RefPtr to the created view
	ultralight::RefPtr<ultralight::View> CreateView(uint32_t width, uint32_t height, ultralight::ViewConfig config = {});

	///
	/// @brief Remove a previously created view from this HUD layer.
	/// @param view The view to remove
	///
	void RemoveView(const ultralight::RefPtr<ultralight::View>& view);

	///
	/// @brief Set the sort order for a view (higher values render on top)
	/// @param view The view to update
	/// @param order Sort order value
	///
	void SetViewSortOrder(const ultralight::RefPtr<ultralight::View>& view, int order);

	///
	/// @brief Enable or disable background blur effect
	/// @param enabled Whether blur should be active
	///
	void SetBackgroundBlur(bool enabled) {
		blur_enabled_ = enabled;
	}

	///
	/// @brief Set the scene framebuffer texture for blur source
	/// @param texId GL texture ID of the scene's color attachment
	///
	void SetSceneTexture(unsigned texId) {
		scene_texture_ = texId;
	}

	///
	/// @brief Check if background blur is currently enabled
	///
	bool IsBackgroundBlurEnabled() const {
		return blur_enabled_;
	}

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
	ultralight::RefPtr<ultralight::View> GetView() const {
		return views_.empty() ? nullptr : views_.front();
	}

	const std::vector<ultralight::RefPtr<ultralight::View>>& GetViews() const {
		return views_;
	}

	///
	/// @brief Get the HUD framebuffer for compositing
	/// @return Pointer to the framebuffer containing the rendered HUD
	///
	Framebuffer* GetFramebuffer() const {
		return framebuffer_.get();
	}

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

	///
	/// @brief Enable input handling for this HUD layer
	/// @note Input is enabled by default when the HUD layer is created
	///
	void EnableInput() {
		input_enabled_ = true;
	}

	///
	/// @brief Disable input handling for this HUD layer
	/// @note Use this when you want game input to pass through without being handled by the HUD
	///
	void DisableInput() {
		input_enabled_ = false;
	}

	///
	/// @brief Check if input handling is enabled
	/// @return true if input is enabled
	///
	bool IsInputEnabled() const {
		return input_enabled_;
	}

	///
	/// @brief Set the viewport offset for coordinate mapping (editor support)
	/// @param x X offset of the viewport in window space
	/// @param y Y offset of the viewport in window space
	///
	void SetViewportOffset(int x, int y) {
		viewport_offset_x_ = x;
		viewport_offset_y_ = y;
	}

	///
	/// @brief Called by the per-instance LoadListener when the main-frame DOM is ready
	///
	void OnDOMReady();

	// Force any queued JS scripts to execute immediately (used by console-ready handshake)
	void FlushPendingScriptsNow();

private:
	/// @brief Create the GPU context and driver
	void CreateGPUContext();

	/// @brief Create the output framebuffer
	void CreateFramebuffer();

	// DOM readiness and script queueing
	bool dom_ready_ = false;                      ///< True after the page's main-frame DOM is ready
	std::unique_ptr<ultralight::LoadListener> load_listener_;
	std::vector<std::string> pending_scripts_;    ///< Scripts queued until DOM ready
	std::string pending_url_;                     ///< URL to load if LoadURL is called before view creation

	/// @brief Evaluate script now or queue it until DOM ready
	void EvalScriptOrQueue(const std::string& script);

	// Singleton instance
	static HUDLayer* s_Instance;

	// Listener for previewing or not te ui
	event::ListenerComponent listener;

	bool active_ = false;

	GLFWwindow* window_ = nullptr;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	float device_scale_ = 1.0f;    ///< Monitor DPI scale (from glfwGetWindowContentScale)
	bool msaa_enabled_ = false;
	bool input_enabled_ = true;    ///< Whether input events are forwarded to Ultralight
	int viewport_offset_x_ = 0;    ///< Viewport X offset in window space (for editor)
	int viewport_offset_y_ = 0;    ///< Viewport Y offset in window space (for editor)

	std::unique_ptr<toast::hud::ToastGPUContext> gpu_context_;
	ultralight::RefPtr<ultralight::Renderer> renderer_;
	std::vector<ultralight::RefPtr<ultralight::View>> views_;
	std::unordered_map<ultralight::View*, int> view_sort_orders_;

	// Output framebuffer for the HUD
	std::unique_ptr<Framebuffer> framebuffer_;
	unsigned read_fbo_ = 0;

	// Background blur
	bool blur_enabled_ = false;
	unsigned scene_texture_ = 0;
	unsigned blur_program_ = 0;
	unsigned blur_fbo_a_ = 0;
	unsigned blur_fbo_b_ = 0;
	unsigned blur_tex_a_ = 0;
	unsigned blur_tex_b_ = 0;
	unsigned blur_vao_ = 0;
	unsigned blur_vbo_ = 0;
	uint32_t blur_tex_width_ = 0;
	uint32_t blur_tex_height_ = 0;

	void InitBlurResources();
	void DestroyBlurResources();
	void RenderBlurToFramebuffer();
	void SortViewsByOrder();
};

}    // namespace renderer::HUD
