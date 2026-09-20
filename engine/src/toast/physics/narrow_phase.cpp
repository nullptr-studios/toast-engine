#include "narrow_phase.hpp"

#include "voxel_query.hpp"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {

auto pairType(ShapeType a, ShapeType b) -> NarrowPhasePairType {
	if (b < a) {
		std::swap(a, b);
	}

	if (a == ShapeType::sphere && b == ShapeType::sphere) {
		return NarrowPhasePairType::sphere_sphere;
	}
	if (a == ShapeType::sphere && b == ShapeType::box) {
		return NarrowPhasePairType::sphere_box;
	}
	if (a == ShapeType::sphere && b == ShapeType::capsule) {
		return NarrowPhasePairType::sphere_capsule;
	}
	if (a == ShapeType::sphere && b == ShapeType::voxel) {
		return NarrowPhasePairType::sphere_voxel;
	}
	if (a == ShapeType::box && b == ShapeType::box) {
		return NarrowPhasePairType::box_box;
	}
	if (a == ShapeType::box && b == ShapeType::capsule) {
		return NarrowPhasePairType::box_capsule;
	}
	if (a == ShapeType::box && b == ShapeType::voxel) {
		return NarrowPhasePairType::box_voxel;
	}
	if (a == ShapeType::capsule && b == ShapeType::voxel) {
		return NarrowPhasePairType::capsule_voxel;
	}
	if (a == ShapeType::voxel && b == ShapeType::voxel) {
		return NarrowPhasePairType::voxel_voxel;
	}
	return NarrowPhasePairType::capsule_capsule;
}

}

auto NarrowPhase::generateManifolds(CollisionWorldView world, std::span<const BroadPhasePair> candidates) const -> ManifoldQueue {
	ZoneScopedN("physics::RecordManifoldQueue");
	ZoneValue(static_cast<uint64_t>(candidates.size()));

	ManifoldQueue queue {.candidate_count = candidates.size()};
	queue.manifolds.reserve(candidates.size());

	std::vector<Manifold> pair_manifolds;
	for (BroadPhasePair pair : candidates) {
		const Shape* shape_a = world.shape(pair.a.shape);
		const Shape* shape_b = world.shape(pair.b.shape);
		if (shape_a && shape_b) {
			++queue.pair_candidates[static_cast<size_t>(pairType(shape_a->type, shape_b->type))];
		}

		pair_manifolds.clear();
		collide(world, pair, pair_manifolds);

		for (Manifold& manifold : pair_manifolds) {
			++queue.collision_count;
			if (validate(world, manifold)) {
				queue.contact_count += manifold.contact_count;
				queue.manifolds.emplace_back(manifold);
			} else {
				++queue.rejected_manifold_count;
			}
		}
	}

	std::ranges::sort(queue.manifolds, [](const Manifold& lhs, const Manifold& rhs) { return lhs < rhs; });
	return queue;
}

auto NarrowPhase::validate(CollisionWorldView world, Manifold& manifold) const -> bool {
	ZoneScopedN("physics::ValidateManifold");
	ZoneValue((static_cast<uint64_t>(manifold.pair.a.shape.slot) << 32) | static_cast<uint64_t>(manifold.pair.b.shape.slot));

	const Body* body_a = world.body(manifold.pair.a.body);
	const Body* body_b = world.body(manifold.pair.b.body);
	const Shape* shape_a = world.shape(manifold.pair.a.shape);
	const Shape* shape_b = world.shape(manifold.pair.b.shape);
	if (!body_a || !body_b || !shape_a || !shape_b || shape_a->owner != manifold.pair.a.body ||
	    shape_b->owner != manifold.pair.b.body) {
		return false;
	}

	if (manifold.contact_count == 0 || manifold.contact_count > manifold.contacts.size()) {
		return false;
	}

	const bool normal_is_finite =
	    std::isfinite(manifold.normal.x) && std::isfinite(manifold.normal.y) && std::isfinite(manifold.normal.z);
	const float normal_length_squared = glm::dot(manifold.normal, manifold.normal);
	if (!normal_is_finite || !std::isfinite(normal_length_squared) ||
	    std::abs(normal_length_squared - 1.0f) > _detail::unit_normal_tolerance) {
		return false;
	}

	for (size_t index = 0; index < manifold.contact_count; ++index) {
		ContactPoint& contact = manifold.contacts[index];
		const bool position_is_finite =
		    std::isfinite(contact.position.x) && std::isfinite(contact.position.y) && std::isfinite(contact.position.z);
		if (!position_is_finite || !std::isfinite(contact.penetration) || contact.penetration < -_detail::contact_tolerance) {
			return false;
		}
		contact.penetration = std::max(contact.penetration, 0.0f);
	}

	return true;
}

void NarrowPhase::flip(Manifold& manifold) {
	manifold.normal = -manifold.normal;
	for (size_t i = 0; i < manifold.contact_count; ++i) {
		std::swap(manifold.contacts[i].feature_a, manifold.contacts[i].feature_b);
	}
}

void NarrowPhase::collide(CollisionWorldView world, BroadPhasePair pair, std::vector<Manifold>& output) const {
	ZoneScopedN("physics::DispatchCollisionPair");
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape* shape_a = world.shape(pair.a.shape);
	const Shape* shape_b = world.shape(pair.b.shape);
	const Body* body_a = world.body(pair.a.body);
	const Body* body_b = world.body(pair.b.body);
	if (!shape_a || !shape_b || !body_a || !body_b || shape_a->owner != pair.a.body || shape_b->owner != pair.b.body) {
		return;
	}

	const CollisionElement element_a {*shape_a, *body_a};
	const CollisionElement element_b {*shape_b, *body_b};

	if (shape_a->type == ShapeType::voxel || shape_b->type == ShapeType::voxel) {
		if (shape_a->type == ShapeType::voxel && shape_b->type == ShapeType::voxel) {
			const auto* voxel_data_a = world.voxelData(shape_a->voxel.data);
			const auto* voxel_data_b = world.voxelData(shape_b->voxel.data);
			if (voxel_data_a && voxel_data_b) {
				collideVoxelVoxel(pair, element_a, *voxel_data_a, element_b, *voxel_data_b, output);
			}
			return;
		}

		const bool voxel_is_a = shape_a->type == ShapeType::voxel;
		const Shape& voxel_shape = voxel_is_a ? *shape_a : *shape_b;
		const CollisionElement primitive_element = voxel_is_a ? element_b : element_a;
		const CollisionElement voxel_element = voxel_is_a ? element_a : element_b;

		const VoxelShapeData* voxel_data = world.voxelData(voxel_shape.voxel.data);
		if (voxel_data == nullptr) {
			return;
		}

		switch (primitive_element.shape.type) {
			case ShapeType::sphere: collideSphereVoxel(pair, primitive_element, voxel_element, *voxel_data, output); break;
			case ShapeType::box: collideBoxVoxel(pair, primitive_element, voxel_element, *voxel_data, output); break;
			case ShapeType::capsule: collideCapsuleVoxel(pair, primitive_element, voxel_element, *voxel_data, output); break;
			case ShapeType::voxel: break;
		}

		if (voxel_is_a) {
			for (Manifold& manifold : output) {
				flip(manifold);
			}
		}
		return;
	}

	std::optional<Manifold> manifold;
	switch (shape_a->type) {
		case ShapeType::sphere:
			switch (shape_b->type) {
				case ShapeType::sphere: manifold = collideSpheres(pair, element_a, element_b); break;
				case ShapeType::box: manifold = collideSphereBox(pair, element_a, element_b); break;
				case ShapeType::capsule: manifold = collideSphereCapsule(pair, element_a, element_b); break;
				case ShapeType::voxel: break;
			}
			break;
		case ShapeType::box:
			switch (shape_b->type) {
				case ShapeType::sphere:
					manifold = collideSphereBox(pair, element_b, element_a);
					if (manifold.has_value()) {
						flip(*manifold);
					}
					break;
				case ShapeType::box: manifold = collideBoxes(pair, element_a, element_b); break;
				case ShapeType::capsule:
					manifold = collideCapsuleBox(pair, element_b, element_a);
					if (manifold.has_value()) {
						flip(*manifold);
					}
					break;
				case ShapeType::voxel: break;
			}
			break;
		case ShapeType::capsule:
			switch (shape_b->type) {
				case ShapeType::sphere:
					manifold = collideSphereCapsule(pair, element_b, element_a);
					if (manifold.has_value()) {
						flip(*manifold);
					}
					break;
				case ShapeType::box: manifold = collideCapsuleBox(pair, element_a, element_b); break;
				case ShapeType::capsule: manifold = collideCapsules(pair, element_a, element_b); break;
				case ShapeType::voxel: break;
			}
			break;
		case ShapeType::voxel: break;
	}

	if (manifold.has_value()) {
		output.push_back(*manifold);
	}
}

}
