#include "joint3d.hpp"

#include "rigidbody.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/renderer/debug/debug_draw.hpp>
#include <toast/renderer/vulkan_renderer.hpp>

namespace physics {

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

void Joint3D::drawDebug() {
	ZoneScoped;
	syncWorldTransform();

	const glm::vec4 color = broken ? glm::vec4(0.5f, 0.5f, 0.5f, 0.8f) : glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);
	const glm::mat3 axes = glm::mat3_cast(world_rotation);
	debug::drawLine(world_position, world_position + (axes[0] * 0.2f), {1.0f, 0.1f, 0.1f, 1.0f});
	debug::drawLine(world_position, world_position + (axes[1] * 0.2f), {0.1f, 1.0f, 0.1f, 1.0f});
	debug::drawLine(world_position, world_position + (axes[2] * 0.2f), {0.2f, 0.4f, 1.0f, 1.0f});
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
