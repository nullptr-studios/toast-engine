#include "node_3d.hpp"

#include "world.hpp"

#include <tracy/Tracy.hpp>

namespace toast {

void Node3D::pos(glm::vec3 pos) {
	const auto delta = pos - m_position;
	m_position = pos;
	m_world_position += delta;
	m_dirty_local = true;
}

auto Node3D::pos() const -> const glm::vec3& {
	return m_position;
}

void Node3D::rotQuat(glm::quat rot) {
	const auto delta = rot - m_rotation;
	m_rotation = rot;
	m_world_rotation += delta;
	m_dirty_local = true;
}

auto Node3D::rotQuat() const -> const glm::quat& {
	return m_rotation;
}

void Node3D::rot(glm::vec3 rad) {
	auto quat = glm::quat(rad);
	rotQuat(quat);
}

auto Node3D::rot() const -> glm::vec3 {
	return glm::eulerAngles(m_rotation);
}

void Node3D::rotDeg(glm::vec3 deg) {
	auto quat = glm::quat(glm::radians(deg));
	rotQuat(quat);
}

auto Node3D::rotDeg() const -> glm::vec3 {
	return glm::degrees(glm::eulerAngles(m_rotation));
}

void Node3D::scale(glm::vec3 scl) {
	const auto delta = scl - m_scale;
	m_scale = scl;
	m_world_scale += delta;
	m_dirty_local = true;
}

auto Node3D::scale() const -> const glm::vec3& {
	return m_scale;
}

void Node3D::lookAt(glm::vec3 target, glm::vec3 up) {
	ZoneScoped;
	ZoneNameF("%s::lookAt()", name().data());

	glm::vec3 forward = target - pos();

	// target is at the exact same position
	if (glm::length(forward) < 0.0001f) {
		return;
	}
	forward = glm::normalize(forward);

	// Calculate the right vector
	if (glm::abs(glm::dot(forward, up)) > 0.999f) {
		up = glm::vec3(1.0f, 0.0f, 0.0f);    // Fallback if looking straight up or down
	}
	glm::vec3 right = glm::normalize(glm::cross(forward, up));

	// Recalculate the orthogonal up vector
	glm::vec3 true_up = glm::cross(right, forward);

	// Construct a rotation matrix from the orthonormal basis
	glm::mat3 rot_mat;
	rot_mat[0] = forward;
	rot_mat[1] = right;
	rot_mat[2] = true_up;
	rotQuat(glm::quat_cast(rot_mat));
}

void Node3D::lookAtZ(glm::vec3 target) {
	ZoneScoped;
	ZoneNameF("%s::lookAtZ()", name().data());

	// Calculate direction vector on the XY plane only
	glm::vec3 forward = target - pos();
	forward.z = 0.0f;    // Flatten the Z axis

	// target is directly above or below
	if (glm::length(forward) < 0.0001f) {
		return;
	}
	forward = glm::normalize(forward);

	glm::vec3 world_up = glm::vec3(0.0f, 0.0f, 1.0f);

	// Compute Right vector
	glm::vec3 right = glm::normalize(glm::cross(forward, world_up));

	// Construct the rotation matrix
	glm::mat3 rot_mat;
	rot_mat[0] = forward;
	rot_mat[1] = right;
	rot_mat[2] = world_up;
	rotQuat(glm::quat_cast(rot_mat));
}

void Node3D::recalculateTransforms() {
	ZoneScoped;
	ZoneNameF("%s::recalculateTransforms()", name().data());

	if (m_dirty_local) {
		m_transform = glm::mat4_cast(m_rotation);

		m_transform[0] *= m_scale.x;
		m_transform[1] *= m_scale.y;
		m_transform[2] *= m_scale.z;

		m_transform[3] = glm::vec4(m_position, 1.0f);

		// do not need to update m_world_pos... since they
		// get updated when we use the setters

		// We update world transform as well
		m_world_transform = m_transform_parent.exists() ? m_transform_parent->getTransform() * m_transform : m_transform;

		World::markNode3DDependantsDirty(box());

		m_dirty_local = false;
	}

	if (m_dirty_world) {
		const glm::mat4 parent_mat = m_transform_parent.exists() ? m_transform_parent->getWorldTransform() : glm::mat4(1.0f);
		m_world_position = parent_mat * glm::vec4(m_position, 1.0f);
		m_world_rotation = glm::quat(parent_mat * glm::mat4_cast(m_rotation));
		m_world_scale = parent_mat * glm::vec4(m_scale, 0.0f);
		m_world_transform = parent_mat * m_transform;

		m_dirty_local = true;
		m_dirty_world = false;
	}
}

auto Node3D::getTransform() noexcept -> const glm::mat4& {
	ZoneScoped;
	ZoneNameF("%s::getTransform()", name().data());

	if (not m_dirty_local) {
		return m_transform;
	}

	recalculateTransforms();
	return m_transform;
}

auto Node3D::getWorldTransform() noexcept -> const glm::mat4& {
	ZoneScoped;
	ZoneNameF("%s::getWorldTransform()", name().data());

	if (not m_dirty_world) {
		return m_world_transform;
	}

	recalculateTransforms();
	return m_world_transform;
}

void Node3D::worldPos(glm::vec3 wpos) {
	const auto delta = wpos - m_world_position;
	m_position += delta;
	m_world_position = wpos;
	m_dirty_local = true;
}

auto Node3D::worldPos() const -> const glm::vec3& {
	return m_world_position;
}

void Node3D::worldRotQuat(glm::quat wrot) {
	const auto delta = wrot - m_world_rotation;
	m_rotation += delta;
	m_world_rotation = wrot;
	m_dirty_local = true;
}

auto Node3D::worldRotQuat() const -> const glm::quat& {
	return m_world_rotation;
}

void Node3D::worldRot(glm::vec3 rad) {
	auto quat = glm::quat(rad);
	worldRotQuat(quat);
}

auto Node3D::worldRot() const -> glm::vec3 {
	return glm::eulerAngles(m_world_rotation);
}

void Node3D::worldRotDeg(glm::vec3 deg) {
	auto quat = glm::quat(glm::radians(deg));
	worldRotQuat(quat);
}

auto Node3D::worldRotDeg() const -> glm::vec3 {
	return glm::degrees(glm::eulerAngles(m_world_rotation));
}

void Node3D::worldScale(glm::vec3 wscl) {
	const auto delta = wscl - m_world_scale;
	m_scale += delta;
	m_world_scale = wscl;
	m_dirty_local = true;
}

auto Node3D::worldScale() const -> const glm::vec3& {
	return m_world_scale;
}

void Node3D::init() {
	// Find the closest Node3D parent
	// we ONLY register dependency on the found one
	for (Box<Node> p = parentInternal(); p.exists(); p = p->parentInternal()) {
		if (Box<Node3D> target = p.as<Node3D>(); target.exists()) {
			m_transform_parent = target;
			// we hold a reference to the transform parent, so it must tick before us
			m_owner->registerDependency(*target, *this);
			break;
		}
	}
}

}
