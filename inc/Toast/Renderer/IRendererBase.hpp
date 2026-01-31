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

#include <array>
#include <glm/glm.hpp>
#include <pplwin.h>
#include <vector>

namespace renderer {


/**
 * @struct RendererConfig
 * @brief Configuration settings for the renderer.
 */
struct RendererConfig {
	glm::uvec2 resolution = glm::uvec2(1920, 1080); 	///< Initial rendering resolution
	bool vSync = true;                            				///< Enable/disable vertical sync
	toast::DisplayMode currentDisplayMode = toast::DisplayMode::WINDOWED;		///< Start in b orderless mode
	//bool hdr = false;
	
	float resolutionScale = 1.0f;											///< Scale factor for main framebuffer resolution
	float lightResolutionScale = .75f;								///< Scale factor for light framebuffer resolution
	
	
	//@FIXME: NOT WORKING!!
	unsigned maxFPS = 500;                      			///< Maximum FPS cap (0 = uncapped)
	
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
	virtual void Clear() = 0;

	/// @brief Resizes the viewport and updates internal render targets
	/// @param width New viewport width in pixels
	/// @param height New viewport height in pixels
	virtual void Resize(glm::uvec2) = 0;

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

	/// @brief Adds a 2D light to the lighting system
	/// @param light Pointer to the light to add
	/// @note The renderer does not take ownership of the light
	virtual void AddLight(Light2D* light) = 0;

	/// @brief Removes a 2D light from the lighting system
	/// @param light Pointer to the light to remove
	virtual void RemoveLight(Light2D* light) = 0;

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

	/// @brief Gets the lighting framebuffer for light accumulation
	/// @return Pointer to the light framebuffer, or nullptr if not applicable
	/// @note This is renderer-specific and may not be available in all implementations
	[[nodiscard]]
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
		if (!resource::ResourceManager::LoadConfig(".\\config\\Renderer.settings", configData)) {
			TOAST_WARN("Failed to load renderer settings file... creating a default one!");
			SaveRenderSettings();
			ApplyRenderSettings();
			return;
		}
		
		try {
			auto j = json_t::parse(configData);
			if (j.contains("resolutionScale")) {
				m_rendererConfig.resolutionScale = j["resolutionScale"].get<float>();
			}
			if (j.contains("lightResolutionScale")) {
				m_rendererConfig.lightResolutionScale = j["lightResolutionScale"].get<float>();
			}
			if (j.contains("vSync")) {
				m_rendererConfig.vSync = j["vSync"].get<bool>();
			}
			if (j.contains("fullscreen")) {
				m_rendererConfig.currentDisplayMode = j["fullscreen"].get<toast::DisplayMode>();
			}
			if (j.contains("maxFPS")) {
				m_rendererConfig.maxFPS = j["maxFPS"].get<unsigned>();
			}
			if (j.contains("resolution")) {
				m_rendererConfig.resolution = j["resolution"].get<glm::uvec2>();
			}
			TOAST_TRACE("SUCCESFULLY LOADED RENDERER SETTINGS!... now applying");
			ApplyRenderSettings();
			
		} catch (const std::exception& e) {
			TOAST_ERROR("Error parsing renderer settings: {0}", e.what());
		}
	}
	
	void SaveRenderSettings() {
		json_t j {};
		j["resolutionScale"] = m_rendererConfig.resolutionScale;
		j["lightResolutionScale"] = m_rendererConfig.lightResolutionScale;
		j["vSync"] = m_rendererConfig.vSync;
		j["fullscreen"] = m_rendererConfig.currentDisplayMode;
		j["maxFPS"] = m_rendererConfig.maxFPS;
		j["resolution"] = m_rendererConfig.resolution;
		
		if (!resource::ResourceManager::SaveConfig(".\\config\\Renderer.settings", j.dump(1))) {
			TOAST_ERROR("Failed to save renderer settings file!");
		} else {
			TOAST_TRACE("SUCCESFULLY SAVED RENDERER SETTINGS!");
		}
	}
	
	virtual void ApplyRenderSettings() = 0;    ///< Applies current render settings to the renderer implementation

	
	void ToggleFullscreen() {
		auto* window = toast::Window::GetInstance();
		if (window->GetDisplayMode() == toast::DisplayMode::FULLSCREEN) {
			window->SetDisplayMode(toast::DisplayMode::WINDOWED);
			m_rendererConfig.currentDisplayMode = toast::DisplayMode::WINDOWED;
		} else {
			window->SetDisplayMode(toast::DisplayMode::FULLSCREEN);
			m_rendererConfig.currentDisplayMode = toast::DisplayMode::FULLSCREEN;
		}
		SaveRenderSettings();
	}
	
	[[nodiscard]]
	const RendererConfig& GetRendererConfig() const noexcept {
		return m_rendererConfig;
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
	std::vector<IRenderable*> m_renderables;    ///< All renderable objects in the scene
	std::vector<Light2D*> m_lights;             ///< All 2D lights in the scene
	bool m_renderablesSortDirty = true;         ///< True when renderables need re-sorting
	bool m_lightsSortDirty = true;              ///< True when lights need re-sorting

	// ========== Transform Matrices ==========
	glm::mat4 m_projectionMatrix = glm::mat4(1.0f);    ///< Camera projection matrix
	glm::mat4 m_viewMatrix = glm::mat4(1.0f);          ///< Camera view matrix
	glm::mat4 m_multipliedMatrix = glm::mat4(1.0f);    ///< Cached projection * view matrix

	// ========== Frustum Culling Data ==========
	std::array<glm::vec4, 6> m_frustumPlanes {};    ///< Frustum planes for culling

	// ========== Global Light ==========
	glm::vec3 m_globalLightColor = glm::vec3(1.0f);    ///< Color of the global ambient light
	float m_globalLightIntensity = 1.f;                ///< Intensity of the global

	bool m_globalLightEnabled = true;                  ///< Whether global light is enabled
	
	// ========== Render Settings ==========
	RendererConfig m_rendererConfig;                   ///< Current renderer configuration
};

inline void LoadRendererSettings() {
	IRendererBase::GetInstance()->LoadRenderSettings();
}

inline void SaveRendererSettings() {
	IRendererBase::GetInstance()->SaveRenderSettings();
}

}    // namespace renderer
