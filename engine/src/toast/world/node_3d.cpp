#include "node_3d.hpp"

#include "world.hpp"

#include <tracy/Tracy.hpp>

namespace toast {

void Node3D::pos(glm::vec3 pos) {
	m_position = pos;
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
}

auto Node3D::pos() const -> const glm::vec3& {
	return m_position;
}

void Node3D::rotQuat(glm::quat rot) {
	m_rotation = glm::normalize(rot);
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
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
	m_scale = scl;
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
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

auto Node3D::up() const -> glm::vec3 {
	return worldRotQuat() * world_up;
}

auto Node3D::forward() const -> glm::vec3 {
	return worldRotQuat() * world_forward;
}

void Node3D::recalculateTransforms() const {
	ZoneScoped;
	ZoneNameF("%s::recalculateTransforms()", name().data());

	// The editor inspector (and anything else going through the reflection system) writes
	// m_position/m_rotation/m_scale directly via FieldAccess::set(), bypassing pos()/rot()/scale() and the
	// dirty-flag bookkeeping they do. Detect that divergence here so such an edit doesn't get stuck behind
	// a cached matrix forever.
	if (!m_dirty_local && (m_position != m_cached_position || m_rotation != m_cached_rotation || m_scale != m_cached_scale)) {
		m_dirty_local = true;
		m_dirty_world = true;
	}

	const bool recomputing_world = m_dirty_world;

	if (m_dirty_local) {
		m_transform = glm::mat4_cast(m_rotation);

		m_transform[0] *= m_scale.x;
		m_transform[1] *= m_scale.y;
		m_transform[2] *= m_scale.z;

		m_transform[3] = glm::vec4(m_position, 1.0f);

		m_cached_position = m_position;
		m_cached_rotation = m_rotation;
		m_cached_scale = m_scale;
		m_dirty_local = false;
	}

	if (m_dirty_world) {
		const glm::mat4 parent_mat = m_transform_parent.exists() ? m_transform_parent->getWorldTransform() : glm::mat4(1.0f);
		m_world_transform = parent_mat * m_transform;
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::decompose(m_world_transform, m_world_scale, m_world_rotation, m_world_position, skew, perspective);
		m_world_rotation = glm::normalize(m_world_rotation);
		m_dirty_world = false;
	}

	// markNode3DDependantsDirty() only reaches nodes whose *closest* Node3D ancestor is this node - i.e. one
	// level of the reduced Node3D tree. Re-issuing it here, every time this node's world transform actually
	// gets recomputed (regardless of whether that was triggered by our own setters, an ancestor's cascade,
	// or the direct-field-write case above), lets each level notify the next one in turn, so grandchildren
	// (a MeshNode nested under an intermediate Node3D group, for example) stay in sync too.
	if (recomputing_world) {
		World::markNode3DDependantsDirty(box());
	}
}

auto Node3D::getTransform() const noexcept -> const glm::mat4& {
	ZoneScoped;
	ZoneNameF("%s::getTransform()", name().data());

	recalculateTransforms();
	return m_transform;
}

auto Node3D::getWorldTransform() const noexcept -> const glm::mat4& {
	ZoneScoped;
	ZoneNameF("%s::getWorldTransform()", name().data());

	recalculateTransforms();
	return m_world_transform;
}

void Node3D::worldPos(glm::vec3 wpos) {
	const glm::mat4 parent_world = m_transform_parent.exists() ? m_transform_parent->getWorldTransform() : glm::mat4(1.0f);
	const glm::vec4 local = glm::inverse(parent_world) * glm::vec4(wpos, 1.0f);
	m_position = glm::vec3(local);
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
}

auto Node3D::worldPos() const -> const glm::vec3& {
	recalculateTransforms();
	return m_world_position;
}

void Node3D::worldRotQuat(glm::quat wrot) {
	const glm::quat parent_world_rot =
	    m_transform_parent.exists() ? m_transform_parent->worldRotQuat() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	m_rotation = glm::normalize(glm::inverse(parent_world_rot) * wrot);
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
}

auto Node3D::worldRotQuat() const -> const glm::quat& {
	recalculateTransforms();
	return m_world_rotation;
}

void Node3D::worldRot(glm::vec3 rad) {
	auto quat = glm::quat(rad);
	worldRotQuat(quat);
}

auto Node3D::worldRot() const -> glm::vec3 {
	recalculateTransforms();
	return glm::eulerAngles(m_world_rotation);
}

void Node3D::worldRotDeg(glm::vec3 deg) {
	auto quat = glm::quat(glm::radians(deg));
	worldRotQuat(quat);
}

auto Node3D::worldRotDeg() const -> glm::vec3 {
	recalculateTransforms();
	return glm::degrees(glm::eulerAngles(m_world_rotation));
}

void Node3D::worldScale(glm::vec3 wscl) {
	const glm::vec3 parent_scale = m_transform_parent.exists() ? m_transform_parent->worldScale() : glm::vec3(1.0f);
	m_scale = glm::vec3(
	    parent_scale.x != 0.0f ? wscl.x / parent_scale.x : wscl.x,
	    parent_scale.y != 0.0f ? wscl.y / parent_scale.y : wscl.y,
	    parent_scale.z != 0.0f ? wscl.z / parent_scale.z : wscl.z
	);
	m_dirty_local = true;
	m_dirty_world = true;
	World::markNode3DDependantsDirty(box());
}

auto Node3D::worldScale() const -> const glm::vec3& {
	recalculateTransforms();
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
