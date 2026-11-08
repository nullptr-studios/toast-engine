#pragma once

#include "aabb_tree.hpp"
#include "body.hpp"
#include "raycast.hpp"
#include "shape.hpp"
#include "voxel_shape_data.hpp"

#include <memory>
#include <toast/world/box.hpp>
#include <utility>
#include <vector>

namespace physics {

/// Immutable copy shared by every snapshot until the key below changes
struct QueryVoxel {
	std::shared_ptr<const VoxelShapeData> data;
	uint32_t generation = 0;
	uint64_t volume_id = 0;
	uint32_t volume_revision = 0;
	uint32_t surface_revision = 0;
};

/// All data used by gameplay collision queries from one completed physics tick.
struct QuerySnapshot {
	AABBTree tree;
	std::vector<BodySlot> bodies;
	std::vector<ShapeSlot> shapes;
	std::vector<QueryVoxel> voxels;
	std::vector<toast::Box<toast::Node>> collider_nodes;
	std::vector<toast::Box<toast::Node>> body_nodes;
	float fat_margin = 0.0f;
	uint64_t tick = 0;

	template<typename Visit>
	void raycast(glm::vec3 origin, glm::vec3 inv_dir, float max_distance, Visit&& visit) const {
		tree.raycast(origin, inv_dir, max_distance, std::forward<Visit>(visit));
	}

	[[nodiscard]]
	auto tryGetShape(ShapeID id) const -> const Shape* {
		if (id.slot >= shapes.size()) {
			return nullptr;
		}
		const ShapeSlot& slot = shapes[id.slot];
		return slot.occupied && slot.generation == id.generation ? &slot.shape : nullptr;
	}

	[[nodiscard]]
	auto tryGetBody(BodyID id) const -> const Body* {
		if (id.slot >= bodies.size()) {
			return nullptr;
		}
		const BodySlot& slot = bodies[id.slot];
		return slot.occupied && slot.generation == id.generation ? &slot.body : nullptr;
	}

	[[nodiscard]]
	auto tryGetVoxelDataConst(VoxelDataID id) const -> const VoxelShapeData* {
		if (id.slot >= voxels.size()) {
			return nullptr;
		}
		const QueryVoxel& slot = voxels[id.slot];
		return slot.data && slot.generation == id.generation ? slot.data.get() : nullptr;
	}

	[[nodiscard]]
	auto colliderFor(ShapeID id) const -> toast::Box<toast::Node> {
		return id.slot < collider_nodes.size() ? collider_nodes[id.slot] : toast::Box<toast::Node> {};
	}

	[[nodiscard]]
	auto nodeFor(BodyID id) const -> toast::Box<toast::Node> {
		return id.slot < body_nodes.size() ? body_nodes[id.slot] : toast::Box<toast::Node> {};
	}
};

[[nodiscard]]
auto snapshotSphereOverlap(const QuerySnapshot& snapshot, glm::vec3 position, float radius, std::optional<LayerFilter> layers)
    -> std::optional<SphereHit>;
[[nodiscard]]
auto snapshotAABBOverlap(const QuerySnapshot& snapshot, const AABB& bounds) -> bool;

}
