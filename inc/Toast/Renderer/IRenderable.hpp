/**
 * @file IRenderable.hpp
 * @author dario
 * @date 28/09/2025
 * @brief Interface for objects that can be rendered.
 *
 * This file provides the IRenderable interface which defines the contract
 * for objects that can be drawn by the rendering system.
 */

#pragma once

#include "Toast/Components/TransformComponent.hpp"

#include <glm/glm.hpp>

namespace renderer {

// this should have been done since the very begining lmao
enum class IRenderablePass : uint8_t {
	GEOMETRY,
	LIGHTS,
	OCCLUSION,
	DIRECTIONAL_SHADOW,
};

/**
 * @class IRenderable
 * @brief Interface for objects that can be rendered by the rendering system.
 *
 * IRenderable extends TransformComponent to provide rendering capabilities.
 * Objects implementing this interface are added to the renderer's queue
 * and drawn during the geometry pass.
 *
 * @par Render Order:
 * Renderables are sorted by Z-depth (front-to-back for opaque objects,
 * back-to-front for transparent objects) to ensure correct visual ordering.
 *
 * @par Implementing IRenderable:
 * @code
 * class SpriteRenderer : public IRenderable {
 * public:
 *     void OnRender(const glm::mat4& viewProjection) noexcept override {
 *         glm::mat4 mvp = viewProjection * GetWorldMatrix();
 *         m_material->Use();
 *         m_material->GetShader()->Set("uMVP", mvp);
 *         m_mesh->Draw();
 *     }
 *
 * private:
 *     std::shared_ptr<Material> m_material;
 *     std::shared_ptr<Mesh> m_mesh;
 * };
 * @endcode
 *
 * @par Registration:
 * Renderables are automatically registered when created and unregistered
 * when destroyed. Use AddRenderable() and RemoveRenderable() on the
 * renderer for manual control.
 *
 * @see IRendererBase, TransformComponent, Material
 */
class IRenderable : public toast::TransformComponent {
public:
	~IRenderable() override = default;

	/**
	 * @brief Called during the geometry pass to render this object.
	 *
	 * Implementations should bind materials/shaders, set uniforms, and
	 * issue draw calls here.
	 *
	 * @param viewProjection Pre-multiplied view-projection matrix.
	 *
	 * @note This method is called in the render loop and must be efficient.
	 * @note Must be noexcept as exceptions cannot be handled in render loop.
	 */
	virtual void OnRender(renderer::IRenderablePass pass, const glm::mat4& viewProjection) noexcept = 0;

	/**
	 * @brief Gets the Z-depth for sorting purposes.
	 *
	 * Returns the world-space Z coordinate used to determine render order.
	 * Lower values are rendered first (farther from camera).
	 *
	 * @return Z-depth in world units.
	 */
	[[nodiscard]]
	virtual float GetDepth() noexcept {
		return worldPosition().z;
	}

	/// Geometry sorting priority. Higher values are rendered later in GeometryPass.
	[[nodiscard]]
	virtual int GetGeometrySortPriority() noexcept {
		return 0;
	}

	/// Camera-space depth key used for transparent sorting.
	/// Higher Z in view space is closer to the camera in the engine's OpenGL convention.
	[[nodiscard]]
	virtual float GetTransparentSortDepth(const glm::mat4& view_matrix) noexcept {
		return (view_matrix * glm::vec4(worldPosition(), 1.0f)).z;
	}

	/// Transparent sorting priority. Higher values are rendered later in SpritePass.
	[[nodiscard]]
	virtual int GetTransparentSortPriority() noexcept {
		return 0;
	}

	/// Whether this renderable should write depth when rendered via transparent geometry path.
	[[nodiscard]]
	virtual bool WritesDepthInGeometryPass() const noexcept {
		return true;
	}

protected:
	IRenderable() = default;
};

}

/// @brief Legacy compatibility alias for global namespace access.
using IRenderable = renderer::IRenderable;
