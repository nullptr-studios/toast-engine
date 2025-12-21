/// @file IRenderable.hpp
/// @author dario
/// @date 28/09/2025.
/// @brief Interface for renderable objects that can be submitted to the renderer

#pragma once

#include "Engine/Toast/Components/TransformComponent.hpp"

#include <glm/glm.hpp>

namespace renderer {

/// @class IRenderable
/// @brief Interface for objects that can be rendered by the rendering system
///
/// This interface extends TransformComponent to provide rendering capabilities.
/// Objects implementing this interface can be added to the renderer's render queue
/// and will have their OnRender method called during the geometry pass.
///
/// The rendering system uses the Z-depth for sorting transparent objects and
/// determining render order.
class IRenderable : public toast::TransformComponent {
public:
	~IRenderable() override = default;

	/// @brief Called during the rendering pass to draw this object
	/// @param viewProjection Pre-computed view-projection matrix for efficient rendering
	/// @note This method must be noexcept as it's called in performance-critical render loop
	virtual void OnRender(const glm::mat4& viewProjection) noexcept = 0;

	/// @brief Gets the Z-depth of this renderable for sorting purposes
	/// @return The world-space Z coordinate used for depth sorting
	/// @note Z-sorting is performed before each render pass for proper rendering order
	/// @note This method is logically const but calls worldPosition() which may update cached matrices
	[[nodiscard]]
	float GetDepth() noexcept {
		return worldPosition().z;
	}

protected:
	IRenderable() = default;
};

}

// Legacy compatibility - allow old code to use global namespace
using IRenderable = renderer::IRenderable;
