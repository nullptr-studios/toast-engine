#include <Engine/Transform2.hpp>
#include <Engine/Toast/Components/TransformComponent.hpp>
#include <Engine/Toast/Objects/Actor.hpp>

using namespace toast;
using namespace glm;

#pragma region TRANSFORM_IMPL

auto TransformImpl::position() const noexcept -> glm::vec3 { return m.position; }
auto TransformImpl::rotation() const noexcept -> glm::quat { return m.rotation; }
auto TransformImpl::rotationRadians() const noexcept -> glm::vec3 { return eulerAngles(m.rotation); }
auto TransformImpl::rotationDegrees() const noexcept -> glm::vec3 { return degrees(eulerAngles(m.rotation)); }
auto TransformImpl::scale() const noexcept -> glm::vec3 { return m.scale; }

auto TransformImpl::position() noexcept -> glm::vec3& {
	m.dirtyInverse = m.dirtyMatrix = true;
	return m.position;
}

auto TransformImpl::rotation() noexcept -> glm::quat& {
	m.dirtyInverse = m.dirtyMatrix = true;
	return m.rotation;
}

auto TransformImpl::scale() noexcept -> glm::vec3& {
	m.dirtyInverse = m.dirtyMatrix = true;
	return m.scale;
}

auto TransformImpl::matrix() const noexcept -> glm::mat4 {
	if (!m.dirtyMatrix) return m.cachedMatrix;

	glm::mat4 translation = glm::translate(glm::mat4(1.0f), m.position);
	glm::mat4 rotation = glm::mat4_cast(m.rotation);
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), m.scale);
	m.cachedMatrix = translation * rotation * scale;
	m.dirtyMatrix = false;
	return m.cachedMatrix;
}

auto TransformImpl::inverse() const noexcept -> glm::mat4 {
	if (!m.dirtyMatrix) return m.dirtyMatrix;

	glm::vec3 inv_scl = 1.0f / m.scale;
	glm::mat4 inv_s = glm::scale(glm::mat4(1.0f), inv_scl);
	glm::mat4 inv_r = glm::mat4_cast(glm::conjugate(m.rotation));
	glm::mat4 inv_t = glm::translate(glm::mat4(1.0f), -m.position);
	m.cachedInverse = inv_s * inv_r * inv_t;
	m.dirtyInverse = false;
	return m.cachedInverse;
}

#pragma endregion

#pragma region TRANSFORM2

auto Transform2::position() const noexcept -> glm::vec3 { return local.position(); }
auto Transform2::position() noexcept -> glm::vec3& { return local.position(); }
auto Transform2::rotation() const noexcept -> glm::quat { return local.rotation(); }
auto Transform2::rotation() noexcept -> glm::quat& { return local.rotation(); }
auto Transform2::rotationRadians() const noexcept -> glm::vec3 { return local.rotationRadians(); }
auto Transform2::rotationDegrees() const noexcept -> glm::vec3 { return local.rotationDegrees(); }
auto Transform2::scale() const noexcept -> glm::vec3 { return local.scale(); }
auto Transform2::scale() noexcept -> glm::vec3& { return local.scale(); }

void Transform2::UpdateWorldTransform(Actor* parent) {
	// TODO: Update this to use Transform2 when available
	vec3 pos = local.position() + parent->transform()->worldPosition();
	if (pos != world.position()) {
		world.position() = pos;
	}

	quat rot = local.rotation() + parent->transform()->worldRotationQuat();
	if (rot != world.rotation()) {
		world.rotation() = rot;
	}

	vec3 sca = local.scale() + parent->transform()->worldScale();
	if (sca != world.scale()) {
		world.scale() = sca;
	}
}

void Transform2::FromTransform(const toast::TransformComponent* t) {
	position() = t->position();
	rotation() = t->rotationQuat();
	scale() = t->scale();
}

void Transform2::ToTransform(toast::TransformComponent* t) const {
	t->position(position());
	t->rotationQuat(rotation());
	t->scale(scale());
}

#pragma endregion

