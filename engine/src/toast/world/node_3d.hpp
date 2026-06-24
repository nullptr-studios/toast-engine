/**
 * @file node_3d.hpp
 * @author Xein
 * @date 07 Jun 2026
 *
 * @brief Node with 3D spatial transforms
 *
 * Caches both local and world-space matrices; only recalculates when a dirty flag is set
 */

#pragma once
#include "box.hpp"
#include "node.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace toast {

class [[ToastNode, Color("Red"), Icon("Box")]] TOAST_API Node3D : public Node {
	friend class World;

public:
	// clang-format off

	/**
	 * @brief Local-space position relative to the parent node
	 * @param pos New position; marks both local and world matrices dirty
	 */
	void pos(glm::vec3 pos);

	/**
	 * @brief Local-space Euler rotation in radians (XYZ order)
	 * @param rad New rotation in radians; stored as a quaternion internally
	 * @note Prefer rotQuat() to avoid gimbal lock
	 */
	void rot(glm::vec3 rad);

	/**
	 * @brief Local-space rotation as a quaternion; the canonical internal representation
	 * @param rot New rotation; no conversion overhead compared to Euler variants
	 */
	void rotQuat(glm::quat rot);

	/**
	 * @brief Convenience wrapper for rot() that accepts degrees instead of radians
	 * @param deg New rotation in degrees per axis
	 */
	void rotDeg(glm::vec3 deg);

	/**
	 * @brief Local-space scale; non-uniform scale is supported
	 * @param scl New scale; all components default to 1
	 */
	void scale(glm::vec3 scl);

	/// Returns the cached local position; no recalculation occurs
	[[nodiscard]] auto pos() const -> const glm::vec3&;

	/**
	 * @brief Local-space Euler rotation in radians, decomposed from the stored quaternion
	 * @note Prefer rotQuat() to avoid gimbal-lock decomposition artifacts
	 */
	[[nodiscard]] auto rot() const -> glm::vec3;

	/// Returns the cached local rotation quaternion; no recalculation occurs
	[[nodiscard]] auto rotQuat() const -> const glm::quat &;

	/// Returns the local rotation in degrees, decomposed from the stored quaternion
	[[nodiscard]] auto rotDeg() const -> glm::vec3;

	/// Returns the cached local scale; no recalculation occurs
	[[nodiscard]] auto scale() const -> const glm::vec3 &;

	/**
	 * @brief Sets the world-space position and back-computes the local position
	 * @param pos Desired position in world space
	 * @note Triggers recalculateTransforms() to resolve the parent's world matrix before the inversion
	 */
	void worldPos(glm::vec3 pos);

	/**
	 * @brief Sets the world-space Euler rotation (radians) and back-computes the local rotation
	 * @param rad Desired rotation in world space, in radians
	 * @note Prefer worldRotQuat() to avoid gimbal lock
	 */
	void worldRot(glm::vec3 rad);

	/**
	 * @brief Sets the world-space rotation quaternion and back-computes the local rotation
	 * @param rot Desired rotation in world space
	 * @note Triggers recalculateTransforms() to resolve the parent's world matrix before the inversion
	 */
	void worldRotQuat(glm::quat rot);

	/**
	 * @brief Sets the world-space rotation in degrees and back-computes the local rotation
	 * @param deg Desired rotation in world space, in degrees
	 */
	void worldRotDeg(glm::vec3 deg);

	/**
	 * @brief Sets the world-space scale and back-computes the local scale
	 * @param scl Desired scale in world space
	 * @note Triggers recalculateTransforms() to resolve the parent's world matrix before the inversion
	 */
	void worldScale(glm::vec3 scl);

	/**
	 * @brief Returns the world-space position, recalculating if m_dirty_world is set
	 * @return Const reference to the cached world position; valid until the next setter call on this node or any ancestor
	 */
	[[nodiscard]] auto worldPos() const -> const glm::vec3&;

	/**
	 * @brief Returns the world-space Euler rotation in radians, recalculating if dirty
	 * @note Prefer worldRotQuat() to avoid gimbal-lock decomposition artifacts
	 */
	[[nodiscard]] auto worldRot() const -> glm::vec3;

	/// Returns the world-space rotation quaternion, recalculating if m_dirty_world is set
	[[nodiscard]] auto worldRotQuat() const -> const glm::quat &;

	/// Returns the world-space rotation in degrees, recalculating if m_dirty_world is set
	[[nodiscard]] auto worldRotDeg() const -> glm::vec3;

	/// Returns the world-space scale, recalculating if m_dirty_world is set
	[[nodiscard]] auto worldScale() const -> const glm::vec3 &;
	// clang-format on

	/**
	 * @brief Orients the node so its forward axis points at a world-space target
	 * @param target World-space point to look at
	 * @param up World-space up vector; defaults to +Z
	 * @note Uses the world transform chain; call after the node's parent hierarchy is set up
	 */
	void lookAt(glm::vec3 target, glm::vec3 up = {0.0f, 0.0f, 1.0f});

	/**
	 * @brief lookAt() constrained to the XY plane; the node faces the target with no roll
	 * @param target World-space point to face; the Z component is ignored
	 */
	void lookAtZ(glm::vec3 target);

protected:
	/**
	 * @brief Returns the local-space 4x4 transform matrix, rebuilding it if m_dirty_local is set
	 * @return Const reference to the cached matrix; valid until the next setter call
	 */
	[[nodiscard]]
	auto getTransform() noexcept -> const glm::mat4&;

	/**
	 * @brief Returns the world-space 4x4 transform matrix, rebuilding it if m_dirty_world is set
	 * @return Const reference to the cached matrix; valid until the next setter call on this node or any ancestor
	 */
	[[nodiscard]]
	auto getWorldTransform() noexcept -> const glm::mat4&;

private:
	bool m_dirty_local = true;
	bool m_dirty_world = true;
	Box<Node3D> m_transform_parent;

	[[Reflect, Unit("m")]] alignas(16) glm::vec3 m_position;
	[[Reflect, Unit("°")]] alignas(16) glm::quat m_rotation;
	[[Reflect]] alignas(16) glm::vec3 m_scale;

	alignas(16) glm::vec3 m_world_position;
	alignas(16) glm::quat m_world_rotation;
	alignas(16) glm::vec3 m_world_scale;
	glm::mat4 m_transform;
	glm::mat4 m_world_transform;

	void recalculateTransforms();

	void init();
};

}
