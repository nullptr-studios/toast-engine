#include "node_3d.hpp"

#include "world.hpp"

#include <tracy/Tracy.hpp>

namespace toast {

namespace {

void composeTransform(glm::mat4& transform, glm::vec3 position, glm::quat rotation, glm::vec3 scale) {
	transform = glm::mat4_cast(glm::normalize(rotation));
	transform[0] *= scale.x;
	transform[1] *= scale.y;
	transform[2] *= scale.z;
	transform[3] = glm::vec4(position, 1.0f);
}

void decomposeTransform(const glm::mat4& transform, glm::vec3& position, glm::quat& rotation, glm::vec3& scale) {
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, position, skew, perspective);
	rotation = glm::normalize(rotation);
}

}

void Node3D::lookAt(glm::vec3 target, glm::vec3 up) {
	ZoneScoped;
	ZoneNameF("%s::lookAt()", name().data());

	glm::vec3 forward = target - position;

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
	rotation = glm::quat_cast(rot_mat);
}

void Node3D::lookAtZ(glm::vec3 target) {
	ZoneScoped;
	ZoneNameF("%s::lookAtZ()", name().data());

	// Calculate direction vector on the XY plane only
	glm::vec3 forward = target - position;
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
	rotation = glm::quat_cast(rot_mat);
}

auto Node3D::up() const -> glm::vec3 {
	return world_rotation * world_up;
}

auto Node3D::forward() const -> glm::vec3 {
	return world_rotation * world_forward;
}

void Node3D::syncTransform() const {
	ZoneScoped;

	const bool local_changed = position != m_previous_position || rotation != m_previous_rotation || scale != m_previous_scale;
	const bool world_changed_by_user = world_position != m_previous_world_position || world_rotation != m_previous_world_rotation ||
	                                   world_scale != m_previous_world_scale;
	const glm::mat4 parent_mat = m_transform_parent.exists() ? m_transform_parent->m_world_transform : glm::mat4(1.0f);

	if (local_changed) {
		composeTransform(m_transform, position, rotation, scale);

		m_previous_position = position;
		m_previous_rotation = rotation;
		m_previous_scale = scale;
		m_dirty_world = true;
	}

	if (world_changed_by_user) {
		glm::vec3 base_world_position;
		glm::quat base_world_rotation;
		glm::vec3 base_world_scale;
		decomposeTransform(parent_mat * m_transform, base_world_position, base_world_rotation, base_world_scale);

		const glm::vec3 world_position_delta = world_position - m_previous_world_position;
		const glm::quat world_rotation_delta = glm::normalize(world_rotation * glm::inverse(m_previous_world_rotation));
		const glm::vec3 world_scale_delta = world_scale - m_previous_world_scale;

		const glm::vec3 desired_world_position = base_world_position + world_position_delta;
		const glm::quat desired_world_rotation = glm::normalize(world_rotation_delta * base_world_rotation);
		const glm::vec3 desired_world_scale = base_world_scale + world_scale_delta;

		glm::mat4 desired_world;
		composeTransform(desired_world, desired_world_position, desired_world_rotation, desired_world_scale);

		// Convert the combined world edit back to local TRS so the next sync starts from the result we just applied.
		const glm::mat4 desired_local = glm::inverse(parent_mat) * desired_world;
		decomposeTransform(desired_local, position, rotation, scale);
		composeTransform(m_transform, position, rotation, scale);

		m_previous_position = position;
		m_previous_rotation = rotation;
		m_previous_scale = scale;
		m_dirty_world = true;
	}

	const bool world_changed = m_dirty_world;
	if (m_dirty_world) {
		m_world_transform = parent_mat * m_transform;

		decomposeTransform(m_world_transform, world_position, world_rotation, world_scale);
		m_previous_world_position = world_position;
		m_previous_world_rotation = world_rotation;
		m_previous_world_scale = world_scale;
		m_dirty_world = false;
	}

	if (world_changed) {
		auto mark_dependants = [this](const Node& node, auto&& self) -> void {
			for (const auto& child : node.children()) {
				if (auto child3d = child.as<Node3D>()) {
					if (child3d->m_transform_parent.exists() && &*child3d->m_transform_parent == this) {
						child3d->m_dirty_world = true;
					}
				} else {
					self(*child, self);
				}
			}
		};
		mark_dependants(*this, mark_dependants);
	}
}

auto Node3D::getTransform() const noexcept -> const glm::mat4& {
	return m_transform;
}

auto Node3D::getWorldTransform() const noexcept -> const glm::mat4& {
	return m_world_transform;
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
