/**
 * @file node_3d.hpp
 * @author Xein
 * @date 07 Jun 2026
 *
 * @brief Node with 3D spatial transforms
 */

#pragma once
#include "box.hpp"
#include "node.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace toast {

class [[ToastNode, Color("Red")]] TOAST_API Node3D : public Node {
	friend class INodeOwner;

public:
	// clang-format off
	[[Reflect, Unit("m")]] alignas(16) mutable glm::vec3 position = glm::vec3(0.0f);
	[[Reflect, Unit("°")]] alignas(16) mutable glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	[[Reflect]]            alignas(16) mutable glm::vec3 scale = glm::vec3(1.0f);

	[[Reflect, Unit("m"), Group("World Transform")]] alignas(16) mutable glm::vec3 world_position = glm::vec3(0.0f);
	[[Reflect, Unit("°"), Group("World Transform")]] alignas(16) mutable glm::quat world_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	[[Reflect, Group("World Transform")]]            alignas(16) mutable glm::vec3 world_scale = glm::vec3(1.0f);
	// clang-format on

	/**
	 * @brief Orients the node so its forward axis points at a world-space target
	 * @param target World-space point to look at
	 * @param up World-space up vector; defaults to +Z
	 */
	void lookAt(glm::vec3 target, glm::vec3 up = {0.0f, 0.0f, 1.0f});

	/**
	 * @brief lookAt() constrained to the XY plane; the node faces the target with no roll
	 * @param target World-space point to face; the Z component is ignored
	 */
	void lookAtZ(glm::vec3 target);

	/**
	 * @return Up vector of the current object in world space
	 */
	[[nodiscard]]
	auto up() const -> glm::vec3;

	/**
	 * @return Forward vector of the current object in world space
	 */
	[[nodiscard]]
	auto forward() const -> glm::vec3;

	void syncTransform() const;

	[[nodiscard]]
	auto getTransform() const noexcept -> const glm::mat4&;

	[[nodiscard]]
	auto getWorldTransform() const noexcept -> const glm::mat4&;

	static constexpr glm::vec3 world_up = {0.0f, 0.0f, 1.0f};
	static constexpr glm::vec3 world_forward = {0.0f, 1.0f, 0.0f};

protected:
	void init();

private:
	mutable bool m_dirty_world = true;
	Box<Node3D> m_transform_parent;

	alignas(16) mutable glm::vec3 m_previous_position = glm::vec3(0.0f);
	alignas(16) mutable glm::quat m_previous_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	alignas(16) mutable glm::vec3 m_previous_scale = glm::vec3(1.0f);
	alignas(16) mutable glm::vec3 m_previous_world_position = glm::vec3(0.0f);
	alignas(16) mutable glm::quat m_previous_world_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	alignas(16) mutable glm::vec3 m_previous_world_scale = glm::vec3(1.0f);

	mutable glm::mat4 m_transform = glm::mat4(1.0f);
	mutable glm::mat4 m_world_transform = glm::mat4(1.0f);
};

}

#include <node3d.generated.hpp>
