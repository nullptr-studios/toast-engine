/**
 * @file node_3d.hpp
 * @author Xein
 * @date 07 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include "box.hpp"
#include "node.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace toast {

class [[ToastNode]] TOAST_API Node3D : public Node {
	friend class World;

public:
	// clang-format off
	void pos(glm::vec3 pos);
	void rot(glm::vec3 rad);
	void rotQuat(glm::quat rot);
	void rotDeg(glm::vec3 deg);
	void scale(glm::vec3 scl);

	[[nodiscard]] auto pos() const -> const glm::vec3&;
	[[nodiscard]] auto rot() const -> const glm::vec3;
	[[nodiscard]] auto rotQuat() const -> const glm::quat &;
	[[nodiscard]] auto rotDeg() const -> const glm::vec3;
	[[nodiscard]] auto scale() const -> const glm::vec3 &;

	void worldPos(glm::vec3 pos);
	void worldRot(glm::vec3 rad);
	void worldRotQuat(glm::quat rot);
	void worldRotDeg(glm::vec3 deg);
	void worldScale(glm::vec3 scl);

	[[nodiscard]] auto worldPos() const -> const glm::vec3&;
	[[nodiscard]] auto worldRot() const -> const glm::vec3;
	[[nodiscard]] auto worldRotQuat() const -> const glm::quat &;
	[[nodiscard]] auto worldRotDeg() const -> const glm::vec3;
	[[nodiscard]] auto worldScale() const -> const glm::vec3 &;
	// clang-format on

	void lookAt(glm::vec3 target, glm::vec3 up = {0.0f, 0.0f, 1.0f});
	void lookAtZ(glm::vec3 target);

protected:
	[[nodiscard]]
	auto getTransform() noexcept -> const glm::mat4&;

	[[nodiscard]]
	auto getWorldTransform() noexcept -> const glm::mat4&;

private:
	bool m_dirty_local = true;
	bool m_dirty_world = true;
	Box<Node3D> m_transform_parent;

	[[Reflect, Group("Transform")]] alignas(16) glm::vec3 m_position;
	[[Reflect, Group("Transform")]] alignas(16) glm::quat m_rotation;
	[[Reflect, Group("Transform")]] alignas(16) glm::vec3 m_scale;

	alignas(16) glm::vec3 m_world_position;
	alignas(16) glm::quat m_world_rotation;
	alignas(16) glm::vec3 m_world_scale;
	glm::mat4 m_transform;
	glm::mat4 m_world_transform;

	void recalculateTransforms();

	void init();
};

}
