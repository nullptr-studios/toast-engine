#include "joint_solver.hpp"
#include "nodes/joint3d.hpp"
#include "simulator.hpp"

#include <toast/voxel/voxel_constants.hpp>

namespace physics {

void Simulator::registerJointNode(Joint3D& node) {
	if (not instance) {
		return;
	}
	if (not onSimulatorThread()) {
		request([box = node.box().as<Joint3D>()]() mutable {
			if (box.exists()) {
				registerJointNode(*box);
			}
		});
		return;
	}
	runPendingRequests();

	const toast::Box<Joint3D> box = node.box().as<Joint3D>();
	const bool known =
	    std::ranges::any_of(instance->m_joint_bindings, [&](const JointBinding& binding) { return binding.node == box; });
	if (not known) {
		instance->m_joint_bindings.push_back(JointBinding {.node = box});
	}
}

void Simulator::unregisterJointNode(Joint3D& node) {
	if (not instance) {
		return;
	}
	if (not onSimulatorThread()) {
		request([box = node.box().as<Joint3D>()]() mutable {
			if (box.exists()) {
				unregisterJointNode(*box);
			}
		});
		return;
	}
	runPendingRequests();

	const toast::Box<Joint3D> box = node.box().as<Joint3D>();
	std::vector<JointBinding>& bindings = instance->m_joint_bindings;
	const auto found = std::ranges::find_if(bindings, [&](const JointBinding& binding) { return binding.node == box; });
	if (found != bindings.end()) {
		instance->destroyJoint(found->joint);
		bindings.erase(found);
	}
}

void Simulator::breakJointNode(Joint3D& node) {
	if (not instance) {
		return;
	}
	if (not onSimulatorThread()) {
		request([box = node.box().as<Joint3D>()]() mutable {
			if (box.exists()) {
				breakJointNode(*box);
			}
		});
		return;
	}
	runPendingRequests();

	const toast::Box<Joint3D> box = node.box().as<Joint3D>();
	for (const JointBinding& binding : instance->m_joint_bindings) {
		if (binding.node == box && instance->valid(binding.joint)) {
			instance->breakJoint(binding.joint, JointBreakReason::requested);
		}
	}
}

void Simulator::repairJointNode(Joint3D& node) {
	if (not instance) {
		return;
	}
	if (not onSimulatorThread()) {
		request([box = node.box().as<Joint3D>()]() mutable {
			if (box.exists()) {
				repairJointNode(*box);
			}
		});
		return;
	}
	runPendingRequests();

	const toast::Box<Joint3D> box = node.box().as<Joint3D>();
	for (JointBinding& binding : instance->m_joint_bindings) {
		if (binding.node == box && binding.broken) {
			binding.broken = false;
			node.broken = false;
		}
	}
}

void Simulator::syncJointNodes() {
	if (m_joint_bindings.empty()) {
		return;
	}
	ZoneScopedN("physics::SyncJointNodes");

	for (size_t index = 0; index < m_joint_bindings.size();) {
		JointBinding& binding = m_joint_bindings[index];
		if (not binding.node.exists()) {
			destroyJoint(binding.joint);
			m_joint_bindings.erase(m_joint_bindings.begin() + static_cast<std::ptrdiff_t>(index));
			continue;
		}
		++index;

		Joint3D& node = *binding.node;
		const JointParams params = node.makeParams();
		const VoxelAnchorSettings anchor {
		  .enabled = node.break_when_anchor_destroyed,
		  .radius_voxels = std::max(node.anchor_radius_voxels, 1),
		  .min_remaining = node.anchor_min_remaining,
		};

		if (valid(binding.joint)) {
			// What the node says now is what the joint does
			Joint& joint = m_joints[binding.joint.slot].joint;
			if (joint.descriptor.params.index() == params.index() && joint.descriptor.params != params) {
				setJointParams(binding.joint, params);
			}
			joint.descriptor.break_force = node.break_force;
			joint.descriptor.break_torque = node.break_torque;
			joint.descriptor.voxel_anchor = anchor;
			continue;
		}
		if (binding.broken) {
			continue;
		}

		const BodyID body_a = node.node_a.exists() ? bodyFor(*node.node_a) : BodyID {};
		const BodyID body_b = node.node_b.exists() ? bodyFor(*node.node_b) : BodyID {};
		if ((node.node_a.exists() && not valid(body_a)) || (node.node_b.exists() && not valid(body_b))) {
			continue;
		}
		if (not valid(body_a) && not valid(body_b)) {
			continue;
		}

		node.syncWorldTransform();
		binding.joint = createJoint(
		    JointDescriptor {
		      .body_a = body_a,
		      .body_b = body_b,
		      .anchor = node.world_position,
		      .frame = node.frameRotation(),
		      .exclude_collision = node.exclude_nodes_from_collision,
		      .params = params,
		      .break_force = node.break_force,
		      .break_torque = node.break_torque,
		      .voxel_anchor = anchor,
		    }
		);
	}
}

void Simulator::publishJoints() {
	for (JointBinding& binding : m_joint_bindings) {
		if (binding.node.exists() && valid(binding.joint)) {
			const Joint& joint = m_joints[binding.joint.slot].joint;
			binding.node->publishState(
			    JointState {
			      .angle = joint.hinge_angle,
			      .position = joint.slider_position,
			      .force = joint.last_force,
			      .torque = joint.last_torque,
			    }
			);
		}
	}

	for (const BrokenJoint& broken : m_broken_joints) {
		for (JointBinding& binding : m_joint_bindings) {
			if (binding.joint == broken.joint) {
				binding.broken = true;
				if (binding.node.exists()) {
					binding.node->markBroken();
				}
			}
		}
	}
	m_broken_joints.clear();
}

void Simulator::breakJoint(JointID joint, JointBreakReason reason) {
	if (not valid(joint)) {
		return;
	}
	m_broken_joints.push_back(BrokenJoint {.joint = joint, .reason = reason});
	// not when the node hears of it
	for (JointBinding& binding : m_joint_bindings) {
		if (binding.joint == joint) {
			binding.broken = true;
		}
	}
	destroyJoint(joint);
	++m_profile.joints_broken;
}

void Simulator::breakOverloadedJoints(float dt) {
	if (m_joint_count == 0) {
		return;
	}

	for (uint32_t index = 0; index < m_joints.size(); ++index) {
		if (index >= m_joint_active.size() || m_joint_active[index] == 0 || not m_joints[index].occupied) {
			continue;
		}

		Joint& joint = m_joints[index].joint;
		const _detail::JointReaction reaction = _detail::jointReaction(joint);
		joint.last_force = reaction.linear / dt;
		joint.last_torque = reaction.angular / dt;

		const JointID id {.slot = index, .generation = m_joints[index].generation};
		if (joint.descriptor.break_force > 0.0f && joint.last_force > joint.descriptor.break_force) {
			breakJoint(id, JointBreakReason::force);
		} else if (joint.descriptor.break_torque > 0.0f && joint.last_torque > joint.descriptor.break_torque) {
			breakJoint(id, JointBreakReason::torque);
		}
	}
}

auto Simulator::findVoxelShape(BodyID body) const -> ShapeID {
	for (uint32_t index = 0; index < m_shapes.size(); ++index) {
		const ShapeSlot& slot = m_shapes[index];
		if (slot.occupied && slot.shape.owner == body && slot.shape.type == ShapeType::voxel) {
			return {.slot = index, .generation = slot.generation};
		}
	}
	return {};
}

auto Simulator::countSolidAround(ShapeID shape_id, const glm::vec3& body_local, int radius_voxels) const
    -> std::optional<uint32_t> {
	const Shape* shape = tryGetShape(shape_id);
	if (shape == nullptr || shape->type != ShapeType::voxel) {
		return std::nullopt;
	}
	const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data);
	if (data == nullptr || data->volume == nullptr) {
		return std::nullopt;
	}

	const glm::vec3 lattice = glm::inverse(shape->voxel.local_rotation) * (body_local - shape->voxel.local_center);
	const glm::ivec3 center = glm::ivec3(glm::floor(lattice / voxel::k_voxel_size));
	uint32_t count = 0;
	for (int z = -radius_voxels; z <= radius_voxels; ++z) {
		for (int y = -radius_voxels; y <= radius_voxels; ++y) {
			for (int x = -radius_voxels; x <= radius_voxels; ++x) {
				const glm::ivec3 cell = center + glm::ivec3(x, y, z);
				if (data->volume->containsVoxel(cell) && data->volume->isSolidAt(cell)) {
					++count;
				}
			}
		}
	}
	return count;
}

auto Simulator::voxelEndOf(BodyID body, const JointFrame& frame, const VoxelAnchorSettings& settings) const -> JointVoxelEnd {
	if (not settings.enabled) {
		return {};
	}

	const ShapeID shape = findVoxelShape(body);
	const std::optional<uint32_t> count = countSolidAround(shape, frame.position, settings.radius_voxels);
	if (not count) {
		return {};
	}

	const Shape* value = tryGetShape(shape);
	const VoxelShapeData* data = tryGetVoxelDataConst(value->voxel.data);
	return JointVoxelEnd {.shape = shape, .baseline = *count, .revision = data->volume->revision()};
}

void Simulator::checkJointAnchors() {
	if (m_joint_count == 0) {
		return;
	}
	ZoneScopedN("physics::CheckJointAnchors");

	for (uint32_t index = 0; index < m_joints.size(); ++index) {
		JointSlot& slot = m_joints[index];
		if (not slot.occupied || not slot.joint.descriptor.voxel_anchor.enabled) {
			continue;
		}

		Joint& joint = slot.joint;
		const VoxelAnchorSettings& settings = joint.descriptor.voxel_anchor;
		bool let_go = false;
		const auto check = [&](BodyID body, const JointFrame& frame, JointVoxelEnd& end) {
			if (end.shape.generation == 0) {
				return;    // not a voxel body
			}

			const Shape* shape = tryGetShape(end.shape);
			if (shape == nullptr) {
				end.shape = findVoxelShape(body);
				end.revision = 0;
				shape = tryGetShape(end.shape);
			}
			if (shape == nullptr) {
				let_go = true;
				return;
			}

			const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data);
			if (data == nullptr || data->volume == nullptr) {
				let_go = true;
				return;
			}
			if (data->volume->revision() == end.revision) {
				return;    // nothing changedd
			}
			end.revision = data->volume->revision();

			const std::optional<uint32_t> count = countSolidAround(end.shape, frame.position, settings.radius_voxels);
			if (not count || static_cast<float>(*count) < static_cast<float>(end.baseline) * settings.min_remaining) {
				let_go = true;
			}
		};

		if (valid(joint.descriptor.body_a)) {
			check(joint.descriptor.body_a, joint.frame_a, joint.voxel_end_a);
		}
		if (valid(joint.descriptor.body_b)) {
			check(joint.descriptor.body_b, joint.frame_b, joint.voxel_end_b);
		}
		if (let_go) {
			breakJoint({.slot = index, .generation = slot.generation}, JointBreakReason::anchor_destroyed);
		}
	}
}

}
