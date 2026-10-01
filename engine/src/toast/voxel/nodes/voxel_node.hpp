/**
 * @file voxel_node.hpp
 * @author dario and Xein
 * @date 11 Sep 2026
 * @brief Frontend for a Voxel node
 */

#pragma once
#include "voxel.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <toast/assets/types.hpp>
#include <toast/physics/body.hpp>
#include <toast/physics/collision.hpp>
#include <toast/voxel/stamp.hpp>
#include <toast/voxel/voxel_edit.hpp>
#include <vector>

namespace assets {
class VoxelModel;
class VoxelPalette;
class PhysicsMaterial;
}

namespace physics {
class Simulator;
}

namespace toast {

namespace _detail {

/** Snaps a position to the voxel it falls in */
[[nodiscard]]
inline auto toVoxel(glm::vec3 pos) noexcept -> glm::ivec3 {
	return {glm::floor(pos)};
}

/** Clamps a script id to a palette id */
[[nodiscard]]
inline auto toId(int id) noexcept -> uint8_t {
	return static_cast<uint8_t>(std::clamp(id, 0, 255));
}

}

/**
 * A shape made of voxels that renders and collides
 *
 * It can start from a voxel model or from nothing
 *
 * @c editShape runs every time the shape is rebuilt and can change it however it wants
 * so it is the perfect function for modifying or creating voxels from code
 *
 * Positions are in voxels relative to the node and each voxel is 10 cm
 *
 * Ids are palette colors with 0 meaning the voxel is empty
 */
class [[ToastNode, Icon("BoxMesh")]] TOAST_API VoxelNode : public Voxel {
	friend class physics::Simulator;

public:
	/** Makes an empty shape */
	VoxelNode() = default;

	/** Makes a shape from a voxel model */
	VoxelNode(assets::Handle<assets::VoxelModel> model) : m_model(std::move(model)) { }

	// Where the shape comes from

	/** @returns The voxel asset */
	[[nodiscard, Reflect]]
	auto getModel() const -> const assets::Handle<assets::VoxelModel>&;

	/** @returns The voxel asset */
	[[nodiscard]]
	auto getModel() -> assets::Handle<assets::VoxelModel>&;

	/** Swaps the model and rebuilds the shape */
	[[Reflect]]
	void setModel(assets::Handle<assets::VoxelModel> model);

	/** The palette that replaces the model colors */
	[[nodiscard, Reflect]]
	auto getPalette() const -> const assets::Handle<assets::VoxelPalette>&;

	/** Replaces the colors the model came with */
	[[Reflect]]
	void setPalette(assets::Handle<assets::VoxelPalette> palette);

	/**
	 * The palette in use as a uid
	 * @return the override first, then the model palette, then 0
	 */
	[[nodiscard, Reflect]]
	auto paletteUid() const -> uint64_t;

	/** The raw loaded model */
	[[nodiscard]]
	auto resolvedModel() const -> const assets::VoxelModel*;

	/** The raw loaded palette in use */
	[[nodiscard]]
	auto resolvedPalette() -> const voxel::Palette*;

	/** The raw physical materials of the palette in use */
	[[nodiscard]]
	auto resolvedMaterialLibrary() -> const voxel::MaterialLibrary*;

	// Building the shape

	/**
	 * @function editShape()
	 * Runs every time the shape is rebuilt so build it here
	 * @note Rebuilds happen on init and when the model or the scripts change
	 * @note Override it in C++ or write it in the Lua script
	 */
	// [[Reflect]]
	// void editShape() { }

	// Reading voxels

	/**
	 * @returns the palette ID at a given position (or 0 if it is empty)
	 */
	[[Reflect]]
	auto getVoxel(glm::vec3 pos) -> int;

	/**
	 * @returns the size of the shape in voxels
	 */
	[[Reflect]]
	auto getSize() -> glm::vec3;

	/**
	 * The lowest and highest solid voxel
	 * @returns an empty list when there are no voxels
	 */
	[[Reflect]]
	auto getBounds() -> std::vector<glm::vec3>;

	/**
	 * @returns how many solid voxels there are
	 */
	[[Reflect]]
	auto getVoxelCount() -> int;

	/**
	 * Turns a world position into a voxel position
	 */
	[[Reflect]]
	auto worldToVoxel(glm::vec3 pos) -> glm::vec3;

	/**
	 * Turns a voxel position into a world position
	 */
	[[Reflect]]
	auto voxelToWorld(glm::vec3 pos) -> glm::vec3;

	// Writing voxels
	//
	// Adding voxels grows the shape on its own
	// Writes with a mode use it to pick what they may overwrite
	//   - replace: Just replaces every voxel
	//   - empty_only: Doesn't touch voxels with a Palette ID different than 0
	//   - solid_only: Doesn't touch empty voxels
	//   - match: Passing an extra palette ID (match_id), it replaces every voxel of that ID

	/** Sets the voxel at pos to id */
	[[Reflect]]
	void setVoxel(glm::vec3 pos, int id);

	/** Empties the voxel at pos */
	[[Reflect]]
	void removeVoxel(glm::vec3 pos);

	/**
	 * Fills the box from min to max with id
	 * @param mode what it is allowed to overwrite
	 * @param match_id the only id that Match overwrites
	 */
	[[Reflect]]
	void fillBox(glm::vec3 min, glm::vec3 max, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	/** Fills a ball around center with id */
	[[Reflect]]
	void fillSphere(glm::vec3 center, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	/** Fills a flat ended cylinder from a to b with id */
	[[Reflect]]
	void fillCylinder(
	    glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	/** Fills a round ended tube from a to b with id */
	[[Reflect]]
	void fillLine(
	    glm::vec3 a, glm::vec3 b, float radius, int id, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	/** Empties the box from min to max */
	[[Reflect]]
	void carveBox(glm::vec3 min, glm::vec3 max);

	/** Empties a ball around center */
	[[Reflect]]
	void carveSphere(glm::vec3 center, float radius);

	/** Empties a flat ended cylinder from a to b */
	[[Reflect]]
	void carveCylinder(glm::vec3 a, glm::vec3 b, float radius);

	/** Empties a round ended tube from a to b */
	[[Reflect]]
	void carveLine(glm::vec3 a, glm::vec3 b, float radius);

	/** Recolors the solid voxels from min to max */
	[[Reflect]]
	void paintBox(glm::vec3 min, glm::vec3 max, int id);

	/** Recolors the solid voxels in a ball */
	[[Reflect]]
	void paintSphere(glm::vec3 center, float radius, int id);

	/** Turns every voxel of one id into another */
	[[Reflect]]
	void replaceId(int from, int to);

	/**
	 * Cuts off everything on one side of a plane
	 * @param side 1 cuts where normal points and minus 1 cuts the other side
	 */
	[[Reflect]]
	void slice(glm::vec3 point, glm::vec3 normal, int side);

	/**
	 * Pulls the flat face at pos out along normal
	 * @param distance how many voxels to move it and negative pushes it in
	 */
	[[Reflect]]
	void extrudeFace(glm::vec3 pos, glm::vec3 normal, int distance);

	/**
	 * Drops a voxel model into the shape at pos
	 * @param rotation snaps to the closest right angle
	 * @note Colors get matched to this palette
	 */
	[[Reflect]]
	void stamp(
	    const assets::Handle<assets::VoxelModel>& asset, glm::vec3 pos, glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	    voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0
	);

	/**
	 * Copies the voxels from min to max
	 * @returns a region number for paste
	 */
	[[Reflect]]
	auto copy(glm::vec3 min, glm::vec3 max) -> int;

	/**
	 * Pastes a copied region with its corner at pos
	 * @note Empty cells are skipped so it never erases
	 */
	[[Reflect]]
	void paste(int region, glm::vec3 pos, voxel::WriteMode mode = voxel::WriteMode::replace, int match_id = 0);

	/** Forgets every copied region */
	[[Reflect]]
	void clearRegions();

	// Shape signals

	signals::Signal<> rebuilt_shape;            ///< Sent after a rebuild once editShape is done
	signals::Signal<int, glm::vec3> damaged;    ///< Sent when hits remove voxels with how many and where in the world
	signals::Signal<int> broke_apart;           ///< Sent when a piece breaks off with how many voxels it took
	signals::Signal<> emptied;                  ///< Sent when the last voxel is gone

	// Physics

	signals::Signal<toast::Box<toast::Node>> contact_begin;    ///< Sent when it starts touching another body
	signals::Signal<toast::Box<toast::Node>> contact_end;      ///< Sent when it stops touching another body
	signals::Signal<> went_to_sleep;                           ///< Sent when the body falls asleep
	signals::Signal<> woke_up;                                 ///< Sent when the body wakes up

	/** Puts the physics body to sleep */
	[[Reflect]]
	void sleep();

	/** Wakes the physics body up */
	[[Reflect]]
	void wake();

	// Editor

	/**
	 * Opens the VoxelEditor
	 */
	[[Reflect, Button("Edit Voxel"), EditorAction("voxel_editor.open")]]
	void editVoxel() { }

	// Backend

	[[nodiscard]]
	auto volume() -> voxel::Volume*;

	[[nodiscard]]
	auto revision() const noexcept -> uint32_t {
		return m_revision;
	}

	[[nodiscard]]
	auto voxelOrigin() const noexcept -> glm::ivec3 {
		return m_voxel_origin;
	}

	[[nodiscard]]
	auto volumeLocalTransform() const -> glm::mat4;

	[[nodiscard, Reflect]]
	auto localBoundingSphere() const -> glm::vec4;

	[[nodiscard]]
	auto latticePlacement() const -> std::optional<voxel::LatticePlacement>;

protected:
	/**
	 * Runs right before editShape on every rebuild, subclasses build their shape here
	 */
	virtual void buildShape() { }

	/**
	 * A hash of everything the shape is built from besides the model
	 */
	[[nodiscard]]
	virtual auto shapeKey() -> uint64_t {
		return 0;
	}

	/** Rebuilds the shape the next time it refreshes */
	void requestRebuild() noexcept { m_rebuild_requested = true; }

	/**
	 * True when the subclass builds its shape on other threads and applies it itself
	 * @note Then the editor tick hands over to tickAsyncBuild and a rebuild only asks for one
	 */
	[[nodiscard]]
	virtual auto buildsAsync() -> bool {
		return false;
	}

	virtual void tickAsyncBuild() { }

	void finishShape();

	void init();
	void begin();
	void end();
	void destroy();
	void onEnable();
	void onDisable();
	void editorTick();
	void earlyTick();
	void postPhysics();
	void onReflectedFieldChanged(std::string_view field_name) override;
	void onScriptsReloaded() override;
	void updateInspectorMessages() override;
	void onEditorTransformChanged() override;
	virtual void drawDebug();

	void refreshVolume();
	void releaseVolume();
	void retireVolume();
	void recordDamage(uint32_t count, glm::vec3 index_sum);

	/**
	 * Grows the volume until bounds fit
	 * @returns null when there is no volume and nothing to add or it would get too big
	 */
	auto ensureContains(const voxel::EditBounds& bounds) -> voxel::Volume*;

	/**
	 * Swaps the volume for an empty one that covers exactly bounds, rounded out to bricks
	 * @returns null when it would get too big, the volume is left alone then
	 */
	auto replaceVolume(const voxel::EditBounds& bounds) -> voxel::Volume*;

	/** Grows for local_bounds when asked and then runs kernel on the volume */
	template<typename Kernel>
	void edit(const voxel::EditBounds& local_bounds, bool grows, std::string_view operation, Kernel&& kernel);

	/** Tells physics and the renderer the shape changed */
	void commitEdit(const voxel::EditResult& result, std::string_view operation);

	[[nodiscard]]
	auto toIndex(glm::ivec3 local) const noexcept -> glm::ivec3 {
		return local - m_voxel_origin;
	}

	[[nodiscard]]
	auto toIndex(glm::vec3 local) const noexcept -> glm::vec3 {
		return local - glm::vec3(m_voxel_origin);
	}

	[[nodiscard]]
	auto toIndex(const voxel::EditBounds& local) const noexcept -> voxel::EditBounds {
		return {local.min - m_voxel_origin, local.max - m_voxel_origin};
	}

	struct ActiveContact {
		physics::BodyID other_body;
		toast::Box<toast::Node> other_node;
		uint32_t shape_pair_count = 0;
	};

	[[nodiscard]]
	auto physicsBound() const noexcept -> bool {
		return m_body != physics::BodyID {};
	}

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

	void handleContactBegin(const physics::BroadPhasePair& pair);
	void handleContactEnd(const physics::BroadPhasePair& pair);

	void applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation);
	void publishPhysicsState(bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity);

	// Inspector

	[[Reflect]]
	assets::Handle<assets::VoxelModel> m_model;

	[[Reflect, Name("Palette Override")]]
	assets::Handle<assets::VoxelPalette> m_palette;

	/** Makes the body static so it never moves */
	[[Reflect]]
	bool indestructible = false;

	[[Reflect, ReadOnly, Unit("kg")]]
	float mass = 0.0f;

	[[Reflect, Group("Physics")]]
	bool allow_sleep = true;

	[[Reflect, Group("Physics"), ReadOnly]]
	bool awake = true;

	[[Reflect, Group("Physics")]]
	float gravity_scale = 1.0f;

	/** Works out the center of mass and inertia from the voxels */
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

	[[Reflect, Name("Show AABB"), Group("AABB")]]
	bool show_aabb = false;

	[[Reflect, Color, Name("AABB Color"), Group("AABB")]]
	glm::vec4 aabb_color = glm::vec4(1.0f, 0.75f, 0.15f, 0.6f);

	/** Draws the box filled instead of as lines */
	[[Reflect, Name("Fill Shape"), Group("AABB")]]
	bool aabb_fill = false;

	std::unique_ptr<voxel::Volume> m_volume;
	std::vector<std::unique_ptr<voxel::Volume>> m_retired_volumes;
	glm::ivec3 m_voxel_origin {0};
	uint32_t m_revision = 0;
	const assets::VoxelModel* m_instanced_from = nullptr;
	assets::Handle<assets::VoxelPalette> m_model_palette;
	std::vector<std::optional<voxel::Region>> m_regions;

	bool m_volume_stale = false;
	bool m_rebuild_requested = false;
	bool m_edit_shape_pending = false;
	bool m_split_pending = false;
	bool m_building_shape = false;

	struct PendingEvents {
		uint32_t damaged_voxels = 0;
		glm::vec3 damaged_sum {0.0f};
		std::vector<int> broken_pieces;
		bool emptied = false;
	};

	PendingEvents m_pending_events;

	physics::BodyID m_body;
	physics::ShapeID m_shape;
	std::vector<ActiveContact> m_active_contacts;
	bool m_registration_requested = false;

	bool m_registered_proxy = false;
	bool m_debug_visible = false;
	uint64_t m_shape_key = 0;

	uint64_t m_reported_wrong_model = 0;
	uint64_t m_reported_wrong_palette = 0;
};

}
