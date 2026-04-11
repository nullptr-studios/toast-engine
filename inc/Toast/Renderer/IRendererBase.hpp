/// @file IRendererBase.hpp
/// @author Dario
/// @date 14/09/25
/// @brief Base interface for all renderer implementations

#pragma once

#include "Camera.hpp"
#include "Toast/Event/ListenerComponent.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Renderer/Framebuffer.hpp"
#include "Toast/Renderer/IRenderable.hpp"
#include "Toast/Renderer/Lights/2DLight.hpp"
#include "Toast/Resources/ResourceManager.hpp"
#include "Toast/Window/Window.hpp"
#include "glm/ext/matrix_clip_space.hpp"

#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

class PostProcessManager;

namespace renderer {

/** @struct RendererConfig
 * @brief Configuration settings for the renderer.
 */
struct RendererConfig {
	glm::uvec2 resolution { 1280, 720 };                                     ///< windowed rendering resolution
	float lightResolutionScale = 1.0f;
	bool vSync { true };                                                     ///< Enable/disable vertical sync
	toast::DisplayMode currentDisplayMode = toast::DisplayMode::WINDOWED;    ///< Current display mode

	unsigned maxFPS = 144;

	float resolutionScale = 1.0f;           ///< Scale factor for main framebuffer resolution
	unsigned msaaSamples = 1;               ///< MSAA sample count for geometry render targets
	float anisotropyLevel = 8.0f;           ///< Texture anisotropy level (clamped by GPU max)
	unsigned shadowMapResolution = 1024;    ///< Square shadow-map/SDF atlas resolution
	unsigned shadowRaymarchSteps = 32;      ///< Raymarch quality for SDF shadows
};

/// @class IRendererBase
/// @brief Abstract base class for all renderer implementations
///
/// This class defines the interface that all renderers must implement and provides
/// common functionality for camera management, scene management, and matrix operations.
/// It uses a singleton pattern to allow global access to the active renderer instance.
///
/// @note Derived classes should implement specific rendering pipelines (forward, deferred, etc.)
class IRendererBase {
public:
	virtual ~IRendererBase() = default;

	// ========== Core Rendering Interface ==========

	/// @brief Main render function, called every frame to render the scene
	/// @note This is where the rendering pipeline is executed
	virtual void Render() = 0;

	/// @brief Clears the current render target
	/// @note Typically clears color and depth buffers
	virtual void Clear() const = 0;

	/// @brief Resizes the viewport and updates internal render targets
	/// @param width New viewport width in pixels
	/// @param height New viewport height in pixels
	virtual void Resize(glm::uvec2) = 0;

	virtual void DrawScreenQuad(bool flipY, bool useShader = true) = 0;

	PostProcessManager* GetPostProcessManager() const {
		return m_postProcessManager.get();
	}

	// ========== ImGui Integration (Editor Only) ==========

	/// @brief Begins a new ImGui frame for editor UI
	/// @note Only available in editor builds (TOAST_EDITOR defined)
	virtual void StartImGuiFrame() = 0;

	/// @brief Ends the current ImGui frame and renders UI
	/// @note Only available in editor builds (TOAST_EDITOR defined)
	virtual void EndImGuiFrame() = 0;

	// ========== Scene Management ==========

	/// @brief Adds a renderable object to the render queue
	/// @param renderable Pointer to the renderable object to add
	/// @note The renderer does not take ownership of the renderable
	virtual void AddRenderable(IRenderable* renderable) = 0;

	/// @brief Removes a renderable object from the render queue
	/// @param renderable Pointer to the renderable object to remove
	virtual void RemoveRenderable(IRenderable* renderable) = 0;
	
	virtual void AddTransparent(IRenderable* renderable) = 0;
	
	virtual void RemoveTransparent(IRenderable* renderable) = 0;

	virtual void AddWater(IRenderable* renderable) = 0;

	virtual void RemoveWater(IRenderable* renderable) = 0;
	
	virtual void DisableTransparent(IRenderable* renderable) {
		if (m_disabledTransparents.insert(renderable).second) {
			m_transparentsSortDirty = true;
		}
	}
	
	virtual void EnableTransparent(IRenderable* renderable) {
		if (m_disabledTransparents.erase(renderable) > 0) {
			m_transparentsSortDirty = true;
		}
	}

	virtual void DisableWater(IRenderable* renderable) {
		if (m_disabledWaters.insert(renderable).second) {
			m_watersSortDirty = true;
		}
	}

	virtual void EnableWater(IRenderable* renderable) {
		if (m_disabledWaters.erase(renderable) > 0) {
			m_watersSortDirty = true;
		}
	}

	virtual void DisableRenderable(IRenderable* renderable) {
		if (m_disabledRenderables.insert(renderable).second) {
			m_renderablesSortDirty = true;
		}
	}

	virtual void EnableRenderable(IRenderable* renderable) {
		if (m_disabledRenderables.erase(renderable) > 0) {
			m_renderablesSortDirty = true;
		}
	}

	/// @brief Adds a 2D light to the lighting system
	/// @param light Pointer to the light to add
	/// @note The renderer does not take ownership of the light
	virtual void AddLight(Light2D* light) = 0;

	/// @brief Removes a 2D light from the lighting system
	/// @param light Pointer to the light to remove
	virtual void RemoveLight(Light2D* light) = 0;

	virtual GLuint GetShadowMapTexture() const = 0;

	// ========== Framebuffer Access ==========

	/// @brief Gets the main output framebuffer containing the final rendered image
	/// @return Pointer to the output framebuffer
	/// @note This is the framebuffer that should be displayed to the screen
	[[nodiscard]]
	Framebuffer* GetMainFramebuffer() const noexcept {
		return m_outputFramebuffer;
	}

	/// @brief Gets the geometry framebuffer (G-buffer for deferred rendering)
	/// @return Pointer to the geometry framebuffer, or nullptr if not applicable
	/// @note This is renderer-specific and may not be available in all implementations
	[[nodiscard]]
	Framebuffer* GetGeometryFramebuffer() const noexcept {
		return m_geometryFramebuffer;
	}

	Framebuffer* GetLightFramebuffer() const noexcept {
		return m_lightFramebuffer;
	}

	// ========== Camera Management ==========

	/// @brief Sets the active camera used for rendering
	/// @param camera Pointer to the camera to use, or nullptr to clear
	void SetActiveCamera(toast::Camera* camera) noexcept {
		m_activeCamera = camera;
	}

	/// @brief Gets the currently active camera
	/// @return Pointer to the active camera, or nullptr if none set
	[[nodiscard]]
	toast::Camera* GetActiveCamera() const noexcept {
		return m_activeCamera;
	}

	// ========== Matrix Operations ==========

	/// @brief Sets the projection matrix directly
	/// @param projection The projection matrix to use
	void SetProjectionMatrix(const glm::mat4& projection) noexcept {
		m_projectionMatrix = projection;
	}

	/// @brief Constructs and sets a perspective projection matrix
	/// @param fovRadians Field of view in radians
	/// @param aspectRatio Aspect ratio (width / height)
	/// @param nearPlane Near clipping plane distance
	/// @param farPlane Far clipping plane distance
	void SetProjectionMatrix(float fovRadians, float aspectRatio, float nearPlane, float farPlane) noexcept {
		m_projectionMatrix = glm::perspective(fovRadians, aspectRatio, nearPlane, farPlane);
	}

	/// @brief Sets the view matrix directly
	/// @param view The view matrix to use
	void SetViewMatrix(const glm::mat4& view) noexcept {
		m_viewMatrix = view;
	}

	/// @brief Gets the current projection matrix
	/// @return Const reference to the projection matrix
	[[nodiscard]]
	const glm::mat4& GetProjectionMatrix() const noexcept {
		return m_projectionMatrix;
	}

	/// @brief Gets the current view matrix
	/// @return Const reference to the view matrix
	[[nodiscard]]
	const glm::mat4& GetViewMatrix() const noexcept {
		return m_viewMatrix;
	}

	/// @brief Gets the pre-multiplied view-projection matrix (projection * view)
	/// @return Const reference to the combined matrix
	/// @note This is computed once per frame for performance
	[[nodiscard]]
	const glm::mat4& GetViewProjectionMatrix() const noexcept {
		return m_multipliedMatrix;
	}

	// ========== Frustum Culling ==========

	/// @brief Gets the frustum planes for culling calculations
	/// @return Array of 6 plane equations (left, right, bottom, top, near, far)
	/// @note Planes are in normalized form (ax + by + cz + d = 0, with normalized (a,b,c))
	[[nodiscard]]
	const std::array<glm::vec4, 6>& GetFrustumPlanes() const noexcept {
		return m_frustumPlanes;
	}

	// ========== Singleton Access ==========

	/// @brief Gets the singleton instance of the active renderer
	/// @return Pointer to the renderer instance, or nullptr if not initialized
	/// @note The first renderer created automatically becomes the singleton instance
	[[nodiscard]]
	static IRendererBase* GetInstance() noexcept {
		return m_instance;
	}

	// ========== Render Settings ==========

	void LoadRenderSettings() {
		std::string configData;
		if (!resource::ResourceManager::LoadConfig("Renderer.settings", configData)) {
			TOAST_WARN("Renderer.settings not found, using defaults");
			ApplySafeRenderSettings();
			return;
		}

		try {
			auto j = json_t::parse(configData);

			if (j.contains("resolutionScale")) {
				m_config.resolutionScale = j["resolutionScale"].get<float>();
			}
			if (j.contains("vSync")) {
				m_config.vSync = j["vSync"].get<bool>();
			}
			if (j.contains("fullscreen")) {
				m_config.currentDisplayMode = j["fullscreen"].get<toast::DisplayMode>();
			}
			if (j.contains("resolution")) {
				m_config.resolution = j["resolution"].get<glm::uvec2>();
			}
			if (j.contains("MaxFPS")) {
				m_config.maxFPS = j["MaxFPS"].get<unsigned>();
			}
			if (j.contains("msaaSamples")) {
				m_config.msaaSamples = std::max(1u, j["msaaSamples"].get<unsigned>());
			}
			if (j.contains("anisotropyLevel")) {
				m_config.anisotropyLevel = std::max(1.0f, j["anisotropyLevel"].get<float>());
			}
			if (j.contains("shadowMapResolution")) {
				m_config.shadowMapResolution = std::clamp(j["shadowMapResolution"].get<unsigned>(), 64u, 8192u);
			}
			if (j.contains("shadowRaymarchSteps")) {
				m_config.shadowRaymarchSteps = std::clamp(j["shadowRaymarchSteps"].get<unsigned>(), 1u, 256u);
			}

			TOAST_TRACE("Renderer.settings loaded successfully");

			// Apply only the settings that don't resize the OS window.
			ApplySafeRenderSettings();

		} catch (const std::exception& e) {
			TOAST_ERROR("Error parsing Renderer.settings: {0} — using defaults", e.what());
			ApplySafeRenderSettings();
		}
	}

	/// @brief Applies only VSync and FPS cap — safe to call at any time, no window resize side-effects.
	void ApplySafeRenderSettings() {
		auto* window = toast::Window::GetInstance();
		window->SetVSync(m_config.vSync);
		window->SetRefreshFrameTime(1000.0 / m_config.maxFPS);
	}

	void SaveRenderSettings() {
		json_t j {};
		j["resolutionScale"] = m_config.resolutionScale;
		j["lightScale"] = m_config.lightResolutionScale;
		j["vSync"] = m_config.vSync;
		j["fullscreen"] = m_config.currentDisplayMode;
		j["resolution"] = m_config.resolution;
		j["MaxFPS"] = m_config.maxFPS;
		j["msaaSamples"] = m_config.msaaSamples;
		j["anisotropyLevel"] = m_config.anisotropyLevel;
		j["shadowMapResolution"] = m_config.shadowMapResolution;
		j["shadowRaymarchSteps"] = m_config.shadowRaymarchSteps;

		if (!resource::ResourceManager::SaveConfig("Renderer.settings", j.dump(1))) {
			TOAST_ERROR("Failed to save renderer settings file!");
		} else {
			// TOAST_TRACE("SUCCESFULLY SAVED RENDERER SETTINGS!");
		}
	}

	virtual void ApplyRenderSettings() = 0;    ///< Applies current render settings to the renderer implementation

	void ToggleFullscreen() {
		auto* window = toast::Window::GetInstance();
		if (window->GetDisplayMode() == toast::DisplayMode::FULLSCREEN) {
			window->SetDisplayMode(toast::DisplayMode::WINDOWED);
			m_config.currentDisplayMode = toast::DisplayMode::WINDOWED;
		} else {
			window->SetDisplayMode(toast::DisplayMode::FULLSCREEN);
			m_config.currentDisplayMode = toast::DisplayMode::FULLSCREEN;
		}
		SaveRenderSettings();
	}

	[[nodiscard]]
	const RendererConfig& GetRendererConfig() const noexcept {
		return m_config;
	}

	/// @brief Sets the maximum FPS cap
	void SetMaxFPS(unsigned fps) noexcept {
		m_config.maxFPS = fps;
	}

	void SetResolutionScale(float scale) noexcept {
		m_config.resolutionScale = std::max(0.1f, scale);
	}

	void SetResolution(glm::uvec2 resolution) noexcept {
		m_config.resolution = resolution;
	}

	void SetDisplayMode(toast::DisplayMode mode) noexcept {
		m_config.currentDisplayMode = mode;
	}

	void SetMsaaSamples(unsigned samples) noexcept {
		m_config.msaaSamples = std::max(1u, samples);
	}

	void SetAnisotropyLevel(float level) noexcept {
		m_config.anisotropyLevel = std::max(1.0f, level);
	}

	void SetShadowMapResolution(unsigned resolution) noexcept {
		m_config.shadowMapResolution = std::clamp(resolution, 64u, 8192u);
	}

	void SetShadowRaymarchSteps(unsigned steps) noexcept {
		m_config.shadowRaymarchSteps = std::clamp(steps, 1u, 256u);
	}

	void SetLightResolutionScale(float scale) noexcept {
		m_config.lightResolutionScale = std::max(0.01f, scale);
	}

	/// @brief Enables or disables VSync and immediately applies it.
	/// Does NOT call the full ApplyRenderSettings
	void SetVSyncEnabled(bool enabled) noexcept {
		m_config.vSync = enabled;
		toast::Window::GetInstance()->SetVSync(enabled);
	}

	// ========== Global Light Settings ==========

	[[nodiscard]]
	glm::vec3 GetGlobalLightColor() const noexcept {
		return m_globalLightColor;
	}

	void SetGlobalLightColor(const glm::vec3& color) noexcept {
		m_globalLightColor = color;
	}

	[[nodiscard]]
	float GetGlobalLightIntensity() const noexcept {
		return m_globalLightIntensity;
	}

	void SetGlobalLightIntensity(float intensity) noexcept {
		m_globalLightIntensity = intensity;
	}

	[[nodiscard]]
	bool IsGlobalLightEnabled() const noexcept {
		return m_globalLightEnabled;
	}

	void SetGlobalLightEnabled(bool enabled) noexcept {
		m_globalLightEnabled = enabled;
	}

protected:
	/// @brief Protected constructor to prevent direct instantiation
	IRendererBase() = default;

	// ========== Singleton Instance ==========
	static IRendererBase* m_instance;

	// ========== Event System ==========
	event::ListenerComponent m_listener;

	// ========== Framebuffers ==========
	/// @note These are owned by the derived renderer implementation
	Framebuffer* m_geometryFramebuffer = nullptr;    ///< G-buffer for deferred rendering (optional)
	Framebuffer* m_lightFramebuffer = nullptr;       ///< Light accumulation buffer (optional)
	Framebuffer* m_outputFramebuffer = nullptr;      ///< Final output framebuffer (required)

	// ========== Camera ==========
	toast::Camera* m_activeCamera = nullptr;    ///< Currently active camera for rendering

	// ========== Scene Objects ==========
	std::vector<IRenderable*> m_renderables;    ///< Opaque renderable objects (geometry pass)
	/// @brief Set of renderables that are currently disabled — excluded from the geometry pass.
	std::unordered_set<IRenderable*> m_disabledRenderables;
	std::unordered_set<IRenderable*> m_disabledTransparents;
	std::unordered_set<IRenderable*> m_disabledWaters;
	std::vector<IRenderable*> m_transparentRenderables;
	std::vector<IRenderable*> m_waterRenderables;
	std::vector<Light2D*> m_lights;        ///< All 2D lights in the scene
	bool m_renderablesSortDirty = true;    ///< True when renderables need re-sorting
	bool m_transparentsSortDirty = true;
	bool m_watersSortDirty = true;
	bool m_lightsSortDirty = true;         ///< True when lights need re-sorting

	// ========== Transform Matrices ==========
	glm::mat4 m_projectionMatrix = glm::mat4(1.0f);    ///< Camera projection matrix
	glm::mat4 m_viewMatrix = glm::mat4(1.0f);          ///< Camera view matrix
	glm::mat4 m_multipliedMatrix = glm::mat4(1.0f);    ///< Cached projection * view matrix

	// ========== Frustum Culling Data ==========
	std::array<glm::vec4, 6> m_frustumPlanes {};    ///< Frustum planes for culling

	// ========== Global Light ==========
	glm::vec3 m_globalLightColor = glm::vec3(1.0f);    ///< Color of the global ambient light
	float m_globalLightIntensity = 0.7f;               ///< Intensity of the global

	bool m_globalLightEnabled = true;                  ///< Whether global light is enabled

	// ========== Post Processing ==========
	std::unique_ptr<PostProcessManager> m_postProcessManager;    ///< Manager for post-processing effects

	// ========== Render Settings ==========
	RendererConfig m_config {};    ///< Current renderer configuration
};

inline void LoadRendererSettings() {
	IRendererBase::GetInstance()->LoadRenderSettings();
}

inline void SaveRendererSettings() {
	IRendererBase::GetInstance()->SaveRenderSettings();
}

}    // namespace renderer
