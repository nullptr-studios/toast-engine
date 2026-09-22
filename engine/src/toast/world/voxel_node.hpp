/**
 * @file voxel_node.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "node_3d.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <toast/assets/types.hpp>
#include <toast/physics/body.hpp>
#include <toast/physics/collision.hpp>
#include <toast/voxel/stamp.hpp>
#include <vector>

namespace assets {
class VoxelModel;
class VoxelPalette;
class VoxelMaterialLibrary;
class PhysicsMaterial;
}

namespace physics {
class Simulator;
}

namespace toast {

class [[ToastNode, Icon("BoxMesh")]] TOAST_API VoxelNode : public Node3D {
	friend class physics::Simulator;

public:
	VoxelNode() = default;

	VoxelNode(assets::Handle<assets::VoxelModel> model) : m_model(std::move(model)) { }

	signals::Signal<toast::Box<toast::Node>> contact_begin;
	signals::Signal<toast::Box<toast::Node>> contact_end;
	signals::Signal<> went_to_sleep;
	signals::Signal<> woke_up;

	[[nodiscard]]
	auto getModel() const -> const assets::Handle<assets::VoxelModel>& {
		return m_model;
	}

	[[nodiscard]]
	auto getModel() -> assets::Handle<assets::VoxelModel>& {
		return m_model;
	}

	void setModel(assets::Handle<assets::VoxelModel> model);

	[[nodiscard]]
	auto getPalette() const -> const assets::Handle<assets::VoxelPalette>& {
		return m_palette;
	}

	void setPalette(assets::Handle<assets::VoxelPalette> palette);

	/// @returns the override else the model palette else 0
	[[nodiscard]]
	auto paletteUid() const -> uint64_t;

	/// @returns centre in xyz radius in w or zero without a model
	[[nodiscard]]
	auto localBoundingSphere() const -> glm::vec4;

	/// @note Call syncTransform() first
	[[nodiscard]]
	auto latticePlacement() const -> std::optional<voxel::LatticePlacement>;

	/// @note Lazy. Main thread only
	[[nodiscard]]
	auto volume() -> voxel::Volume*;

	[[nodiscard]]
	auto resolvedPalette() -> const voxel::Palette*;
	[[nodiscard]]
	auto resolvedModel() const -> const assets::VoxelModel*;
	[[nodiscard]]
	auto resolvedMaterialLibrary() -> const voxel::MaterialLibrary*;

	[[nodiscard]]
	auto revision() const noexcept -> uint32_t {
		return m_revision;
	}

	[[Reflect]]
	void sleep();
	[[Reflect]]
	void wake();

private:
	struct ActiveContact {
		physics::BodyID other_body;
		toast::Box<toast::Node> other_node;
		uint32_t shape_pair_count = 0;
	};

	void updateInspectorMessages() override;
	void init();
	void begin();
	void end();
	void destroy();
	void onEnable();
	void onDisable();
	void drawDebug();

	void releaseVolume();
	void retireVolume();

	[[nodiscard]]
	auto physicsBound() const noexcept -> bool {
		return m_body != physics::BodyID {};
	}

	void handleContactBegin(const physics::BroadPhasePair& pair);
	void handleContactEnd(const physics::BroadPhasePair& pair);

	void applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation);
	void publishPhysicsState(bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity);

	void assignBody(physics::BodyID body) noexcept { m_body = body; }

	void assignShape(physics::ShapeID shape) noexcept { m_shape = shape; }

	[[nodiscard]]
	auto bodyID() const noexcept -> physics::BodyID {
		return m_body;
	}

	[[nodiscard]]
	auto shapeID() const noexcept -> physics::ShapeID {
		return m_shape;
	}

	[[Reflect]]
	assets::Handle<assets::VoxelModel> m_model;

	[[Reflect, Name("Palette Override")]]
	assets::Handle<assets::VoxelPalette> m_palette;

	[[Reflect]]
	bool indestructible = false;

	[[Reflect]]
	bool allow_sleep = true;

	[[Reflect, ReadOnly]]
	bool awake = true;

	[[Reflect, Name("Show AABB"), Group("AABB")]]
	bool show_aabb = false;

	[[Reflect, Color, Name("AABB Color"), Group("AABB")]]
	glm::vec4 aabb_color = glm::vec4(1.0f, 0.75f, 0.15f, 0.6f);

	[[Reflect, Name("Fill Shape"), Group("AABB")]]
	bool aabb_fill = false;

	[[Reflect, Group("Physics")]]
	float gravity_scale = 1.0f;

	[[Reflect, Group("Physics")]]
	bool auto_mass_center = true;

	[[Reflect, Group("Physics"), Unit("m"), ReadOnly("auto_mass_center")]]
	glm::vec3 center_of_mass = {};

	[[Reflect, Group("Physics"), Unit("kg•m²"), ReadOnly("auto_mass_center")]]
	glm::vec3 inertia = {};

	[[Reflect, Group("Physics"), Subgroup("Velocities"), Unit("m/s"), ReadOnly]]
	glm::vec3 linear_velocity = {};
	[[Reflect, Group("Physics"), Subgroup("Velocities"), Unit("rad/s"), ReadOnly]]
	glm::vec3 angular_velocity = {};

	[[Reflect, Group("Physics"), Subgroup("Constant Forces"), Unit("N"), ReadOnly]]
	glm::vec3 constant_force = {};
	[[Reflect, Group("Physics"), Subgroup("Constant Forces"), Unit("N•m"), ReadOnly]]
	glm::vec3 constant_torque = {};

	[[Reflect, Name("Lock X"), Group("Physics"), Subgroup("Position Locks"), ReadOnly]]
	bool lock_pos_x = false;
	[[Reflect, Name("Lock Y"), Group("Physics"), Subgroup("Position Locks"), ReadOnly]]
	bool lock_pos_y = false;
	[[Reflect, Name("Lock Z"), Group("Physics"), Subgroup("Position Locks"), ReadOnly]]
	bool lock_pos_z = false;
	[[Reflect, Name("Lock X"), Group("Physics"), Subgroup("Rotation Locks"), ReadOnly]]
	bool lock_rot_x = false;
	[[Reflect, Name("Lock Y"), Group("Physics"), Subgroup("Rotation Locks"), ReadOnly]]
	bool lock_rot_y = false;
	[[Reflect, Name("Lock Z"), Group("Physics"), Subgroup("Rotation Locks"), ReadOnly]]
	bool lock_rot_z = false;

	bool m_registered_proxy = false;
	bool m_registration_requested = false;
	bool m_debug_visible = false;

	physics::BodyID m_body;
	physics::ShapeID m_shape;
	std::vector<ActiveContact> m_active_contacts;

	std::unique_ptr<voxel::Volume> m_volume;

	/// Kept until physics rebinds since its shape points at the old volume
	std::vector<std::unique_ptr<voxel::Volume>> m_retired_volumes;

	bool m_volume_stale = false;

	const assets::VoxelModel* m_instanced_from = nullptr;

	assets::Handle<assets::VoxelPalette> m_model_palette;
	assets::Handle<assets::VoxelMaterialLibrary> m_material_library;

	uint32_t m_revision = 0;

	uint64_t m_reported_wrong_model = 0;
	uint64_t m_reported_wrong_palette = 0;
};

}
