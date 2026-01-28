/**
 * @file Camera.hpp
 * @author Dario
 * @date 17/09/25
 * @brief Camera actor for controlling the view into the scene.
 *
 * This file provides the Camera class which controls what the player
 * sees in the game world by providing the view matrix for rendering.
 */

#pragma once
#include "Toast/Objects/Actor.hpp"

#include <glm/glm.hpp>

namespace toast {

/**
 * @class Camera
 * @brief Actor that provides the view matrix for rendering.
 *
 * The Camera class extends Actor to provide camera functionality.
 * It calculates the view matrix from its transform and can be set
 * as the active camera for the renderer.
 *
 * @par Usage Example:
 * @code
 * // Create a camera
 * auto* camera = scene->children.Add<Camera>("MainCamera");
 *
 * // Position it
 * camera->transform()->position({ 0.0f, 0.0f, 10.0f });
 *
 * // Make it the active camera
 * camera->SetActiveCamera(true);
 *
 * // Follow a target in Tick()
 * void Tick() override {
 *     glm::vec3 targetPos = target->transform()->worldPosition();
 *     transform()->position(glm::lerp(transform()->position(), targetPos, 5.0f * Time::delta()));
 * }
 * @endcode
 *
 * @note Only one camera should be active at a time. Setting a new
 *       active camera automatically deactivates the previous one.
 *
 * @see Actor, IRendererBase
 */
class Camera : public toast::Actor {
public:
	REGISTER_TYPE(Camera);

	/**
	 * @brief Initializes the camera.
	 */
	void Init() override;

	/**
	 * @brief Called when the camera becomes active.
	 */
	void Begin() override;

	/**
	 * @brief Cleans up the camera when destroyed.
	 */
	void Destroy() override;

	[[nodiscard]]
	json_t Save() const override;

	void Load(json_t j, bool force_create = true) override;

	/**
	 * @brief Gets the view matrix for rendering.
	 *
	 * The view matrix transforms world coordinates to camera space.
	 * It is the inverse of the camera's world transform matrix.
	 *
	 * @return The 4x4 view matrix.
	 */
	[[nodiscard]]
	glm::mat4 GetViewMatrix() const;

	/**
	 * @brief Checks if this is the active camera.
	 * @return true if this camera is currently active.
	 */
	[[nodiscard]]
	bool IsActiveCamera() const {
		return m_isActiveCamera;
	}

	/**
	 * @brief Sets this camera as the active camera.
	 *
	 * When set to true, this camera becomes the active camera for
	 * the renderer. The previous active camera is deactivated.
	 *
	 * @param active true to make this the active camera.
	 */
	void SetActiveCamera(bool active);

private:
	/// @brief Whether this camera is currently active.
	bool m_isActiveCamera = false;

	/// @brief Cached view matrix.
	alignas(16) glm::mat4 m_viewMatrix = glm::mat4(1.0f);
};

}
