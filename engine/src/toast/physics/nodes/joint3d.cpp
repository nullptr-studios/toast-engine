#include "joint3d.hpp"

#include "rigidbody.hpp"

#include <glm/gtc/quaternion.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/renderer/debug/debug_draw.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/nodes/voxel_node.hpp>

namespace physics {

namespace {

const glm::vec4 k_color {1.0f, 0.6f, 0.1f, 1.0f};
const glm::vec4 k_broken_color {0.5f, 0.5f, 0.5f, 1.0f};

auto hasBody(const toast::Box<toast::Node3D>& node) -> bool {
	for (toast::Box<toast::Node> current = toast::Box<toast::Node>(node); current.exists(); current = current->parent()) {
		if (current.as<Rigidbody>().exists() || current.as<toast::VoxelNode>().exists()) {
			return true;
		}
	}
	return false;
}

}

void Joint3D::updateInspectorMessages() {
	static const toast::NodeMessage empty_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 10,
	  .text = "A joint needs node_a or node_b to hold something",
	};
	static const toast::NodeMessage same_message {
	  .severity = toast::NodeMessage::error,
	  .id = 11,
	  .text = "node_a and node_b are the same node",
	};

	if (node_a.exists() || node_b.exists()) {
		removeInspectorMessage(empty_message);
	} else {
		addInspectorMessage(empty_message);
	}

	if (node_a.exists() && node_b.exists() && node_a == node_b) {
		addInspectorMessage(same_message);
	} else {
		removeInspectorMessage(same_message);
	}
	static const toast::NodeMessage no_body_a_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 12,
	  .text = "node_a can't find a rigidbody",
	};
	static const toast::NodeMessage no_body_b_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 13,
	  .text = "node_b can't find a rigidbody",
	};
	if (node_a.exists() && not hasBody(node_a)) {
		addInspectorMessage(no_body_a_message);
	} else {
		removeInspectorMessage(no_body_a_message);
	}
	if (node_b.exists() && not hasBody(node_b)) {
		addInspectorMessage(no_body_b_message);
	} else {
		removeInspectorMessage(no_body_b_message);
	}
}

void Joint3D::init() {
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->registerDebugDraw(this, [](toast::Node3D& node) {
			static_cast<Joint3D&>(node).drawDebug();
		});
	}
}

void Joint3D::destroy() {
	if (renderer::VulkanRenderer::instance) {
		renderer::VulkanRenderer::instance->unregisterDebugDraw(this);
	}
}

void Joint3D::begin() {
	if (not m_registered && participatesIn(toast::NodeOwnerParticipation::gameplay_tick)) {
		m_registered = true;
		Simulator::registerJointNode(*this);
	}
}

void Joint3D::end() {
	if (m_registered) {
		m_registered = false;
		Simulator::unregisterJointNode(*this);
	}
}

void Joint3D::breakJoint() {
	Simulator::breakJointNode(*this);
}

void Joint3D::repair() {
	Simulator::repairJointNode(*this);
}

void Joint3D::publishState(const JointState& state) {
	current_force = state.force;
	current_torque = state.torque;
	applyState(state);
}

void Joint3D::markBroken() {
	broken = true;
	current_force = 0.0f;
	current_torque = 0.0f;
	joint_broken.fire();
}

auto Joint3D::frameRotation() const -> glm::quat {
	syncWorldTransform();
	const glm::mat4& world = getWorldTransform();
	glm::vec3 x {world[0]};
	glm::vec3 y {world[1]};
	glm::vec3 z {world[2]};

	const auto usable = [](const glm::vec3& axis) { return glm::dot(axis, axis) > 1.0e-12f; };
	if (not usable(x) && usable(y) && usable(z)) {
		x = glm::cross(y, z);
	} else if (not usable(y) && usable(x) && usable(z)) {
		y = glm::cross(z, x);
	}
	if (not usable(x) || not usable(y)) {
		return world_rotation;
	}

	x = glm::normalize(x);
	y = glm::normalize(y - (x * glm::dot(x, y)));
	return glm::normalize(glm::quat_cast(glm::mat3 {x, y, glm::cross(x, y)}));
}

void Joint3D::drawDebug() {
	ZoneScoped;
	if (not draw_debug) {
		return;
	}
	syncWorldTransform();

	const glm::vec4 color = broken ? k_broken_color : k_color;
	const glm::mat3 axes = glm::mat3_cast(frameRotation());
	drawGizmo(world_position, axes, color);
	const glm::vec4 link {color.r, color.g, color.b, 0.35f};
	for (const toast::Box<toast::Node3D>& end : {node_a, node_b}) {
		if (end.exists()) {
			end->syncWorldTransform();
			debug::drawLine(world_position, end->world_position, link);
		}
	}
}

}
