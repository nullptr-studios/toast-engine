#include "broad_phase.hpp"
#include "manifold.hpp"
#include "physics_settings.hpp"
#include "simulator.hpp"
#include "voxel_query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {

constexpr float k_approach_epsilon = 1.0e-4f;
constexpr float k_kinematic_wake_margin = 0.05f;

auto capsuleProbe(BodyID owner, const CapsuleShape& capsule) -> Shape {
	return Shape {.owner = owner, .type = ShapeType::capsule, .capsule = capsule};
}

auto sphereProbe(const SphereShape& sphere) -> Shape {
	return Shape {.type = ShapeType::sphere, .sphere = sphere};
}

auto boxProbe(const AABB& bounds) -> Shape {
	return Shape {.type = ShapeType::box, .box = BoxShape {.size = bounds.max - bounds.min}};
}

auto probeBody(const glm::vec3& position, const glm::quat& rotation) -> Body {
	return Body {.type = BodyType::kinematic_body, .position = position, .rotation = rotation};
}

auto boundsVolume(const AABB& bounds) -> float {
	const glm::vec3 size = bounds.max - bounds.min;
	return size.x * size.y * size.z;
}

struct SweepVoxel {
	glm::vec3 min;
	uint8_t normal_index;
};

struct SweepTarget {
	ShapeID shape;
	BodyID body;
	const VoxelShapeData* voxel_data = nullptr;
	glm::mat3 basis {1.0f};
	glm::vec3 origin {};
	glm::vec3 local_a {};
	glm::vec3 local_b {};
	glm::vec3 local_delta {};
	bool gathered = false;
	size_t first_voxel = 0;
	size_t voxel_count = 0;
};

struct SweepScratch {
	std::vector<SweepTarget> targets;
	std::vector<SweepVoxel> voxels;
	std::vector<QueryContact> contacts;
};

auto sweepScratch() -> SweepScratch& {
	thread_local SweepScratch scratch;    // reuse instead of reallocate new vector
	return scratch;
}

auto probeManifolds() -> std::vector<Manifold>& {
	thread_local std::vector<Manifold> manifolds;
	return manifolds;
}

void voxelSweepContacts(
    const SweepTarget& target, std::span<const SweepVoxel> gathered, float radius, float t, std::vector<QueryContact>& contacts
) {
	const glm::vec3 shift = target.local_delta * t;
	const glm::vec3 local_a = target.local_a + shift;
	const glm::vec3 local_b = target.local_b + shift;
	const AABB local_bounds {
	  .min = glm::min(local_a, local_b) - glm::vec3(radius),
	  .max = glm::max(local_a, local_b) + glm::vec3(radius),
	};

	std::array<std::optional<_detail::CapsuleVoxelContact>, voxel::k_normal_direction_count> best_per_normal;
	const auto consider = [&](const glm::vec3& voxel_min, uint8_t normal_index) {
		std::optional<_detail::CapsuleVoxelContact> contact =
		    _detail::capsuleVoxelContact(local_a, local_b, radius, voxel_min, normal_index);
		if (not contact.has_value()) {
			return;
		}
		std::optional<_detail::CapsuleVoxelContact>& slot = best_per_normal[normal_index];
		if (not slot.has_value() || slot->penetration < contact->penetration) {
			slot = contact;
		}
	};

	if (target.gathered) {
		const glm::vec3 voxel_extent {voxel::k_voxel_size};
		for (const SweepVoxel& voxel : gathered.subspan(target.first_voxel, target.voxel_count)) {
			// the same voxels the walk over local_bounds would visit
			if (glm::any(glm::lessThanEqual(voxel.min + voxel_extent, local_bounds.min)) ||
			    glm::any(glm::greaterThanEqual(voxel.min, local_bounds.max))) {
				continue;
			}
			consider(voxel.min, voxel.normal_index);
		}
	} else {
		const VoxelShapeData& data = *target.voxel_data;
		const VoxelQueryContext context {
		  .volume = *data.volume, .surface = data.surface, .palette = data.palette, .materials = data.materials
		};
		queryVoxelSurface(context, local_bounds, [&](const VoxelCandidate& candidate) {
			if (candidate.normal_index < voxel::k_normal_direction_count) {
				consider(candidate.min, candidate.normal_index);
			}
		});
	}

	for (const std::optional<_detail::CapsuleVoxelContact>& best : best_per_normal) {
		if (not best.has_value()) {
			continue;
		}
		const glm::vec3 capsule_surface = target.origin + target.basis * best->local_surface;
		const glm::vec3 voxel_surface = target.origin + target.basis * best->local_point;
		contacts.push_back(
		    QueryContact {
		      .body = target.body,
		      .shape = target.shape,
		      .normal = -(target.basis * best->local_normal),
		      .point = (capsule_surface + voxel_surface) * 0.5f,
		      .penetration = best->penetration,
		    }
		);
	}
}

}

auto Simulator::current() -> Simulator* {
	return instance;
}

auto Simulator::queryCandidates(BodyID ignored, const AABB& bounds) const -> std::vector<ShapeID> {
	std::vector<ShapeID> result;
	// the tree is refit once per step so bodies may have drifted up to one margin since
	for (const ShapeID shape_id : m_broad_phase.queryBounds(bounds.expanded(tunables().broadphase_fat_margin))) {
		const Shape* shape = tryGetShape(shape_id);
		if (shape == nullptr || not shape->enabled || shape->owner == ignored) {
			continue;
		}
		const Body* body = tryGetBody(shape->owner);
		if (body == nullptr || not body->enabled) {
			continue;
		}
		result.push_back(shape_id);
	}
	return result;
}

void Simulator::collideCapsuleProbe(
    const Body& probe_body, const Shape& probe_shape, std::span<const ShapeID> candidates, std::vector<QueryContact>& contacts
) const {
	std::vector<Manifold>& manifolds = probeManifolds();
	for (const ShapeID shape_id : candidates) {
		const Shape* shape = tryGetShape(shape_id);
		const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
		if (body == nullptr) {
			continue;
		}

		const BroadPhasePair pair {
		  .a = {.body = probe_shape.owner},
        .b = {.body = shape->owner, .shape = shape_id}
		};
		const CollisionElement probe {probe_shape, probe_body};
		const CollisionElement other {*shape, *body};

		// every manifold ends up with the probe as A so the normal points from the probe into the obstacle
		manifolds.clear();
		switch (shape->type) {
			case ShapeType::sphere:
				if (auto manifold = collideSphereCapsule(pair, other, probe)) {
					manifold->normal = -manifold->normal;
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::box:
				if (auto manifold = collideCapsuleBox(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::capsule:
				if (auto manifold = collideCapsules(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::voxel:
				if (const VoxelShapeData* data = tryGetVoxelData(shape->voxel.data); data != nullptr && data->volume != nullptr) {
					collideCapsuleVoxel(pair, probe, other, *data, manifolds);
				}
				break;
		}

		for (const Manifold& manifold : manifolds) {
			if (manifold.contact_count == 0) {
				continue;
			}
			const auto points = std::span {manifold.contacts}.first(manifold.contact_count);
			const ContactPoint& deepest = *std::ranges::max_element(points, {}, &ContactPoint::penetration);
			contacts.push_back(
			    QueryContact {
			      .body = shape->owner,
			      .shape = shape_id,
			      .normal = -manifold.normal,
			      .point = deepest.position,
			      .penetration = deepest.penetration,
			    }
			);
		}
	}
}

void Simulator::overlapShape(
    BodyID ignored, const Shape& probe_shape, const glm::vec3& position, const glm::quat& rotation, std::vector<BodyID>& bodies
) const {
	ZoneScopedN("physics::OverlapShape");

	bodies.clear();
	const Tunables& settings = tunables();
	const Body probe_body = probeBody(position, rotation);
	const CollisionElement probe {probe_shape, probe_body};

	std::vector<Manifold> manifolds;
	for (const ShapeID shape_id : queryCandidates(ignored, worldShapeBounds(probe_body, probe_shape))) {
		const Shape* shape = tryGetShape(shape_id);
		const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
		if (body == nullptr || std::ranges::find(bodies, shape->owner) != bodies.end()) {
			continue;
		}

		bool checked = false;
		switch (body->type) {
			case BodyType::dynamic_body: checked = settings.trigger_check_dynamic; break;
			case BodyType::kinematic_body: checked = settings.trigger_check_kinematic; break;
			default: checked = false; break;
		}

		if (not checked) {
			continue;
		}

		const BroadPhasePair pair {
		  .a = {.body = probe_shape.owner},
        .b = {.body = shape->owner, .shape = shape_id}
		};
		const CollisionElement other {*shape, *body};
		const VoxelShapeData* voxel_data = nullptr;
		if (shape->type == ShapeType::voxel) {
			voxel_data = tryGetVoxelData(shape->voxel.data);
			if (voxel_data == nullptr || voxel_data->volume == nullptr) {
				continue;
			}
		}

		// normals are irrelevant here
		manifolds.clear();
		const auto keep = [&manifolds](std::optional<Manifold> manifold) {
			if (manifold) {
				manifolds.push_back(*manifold);
			}
		};
		switch (probe_shape.type) {
			case ShapeType::sphere:
				switch (shape->type) {
					case ShapeType::sphere: keep(collideSpheres(pair, probe, other)); break;
					case ShapeType::box: keep(collideSphereBox(pair, probe, other)); break;
					case ShapeType::capsule: keep(collideSphereCapsule(pair, probe, other)); break;
					case ShapeType::voxel: collideSphereVoxel(pair, probe, other, *voxel_data, manifolds); break;
				}
				break;
			case ShapeType::box:
				switch (shape->type) {
					case ShapeType::sphere: keep(collideSphereBox(pair, other, probe)); break;
					case ShapeType::box: keep(collideBoxes(pair, probe, other)); break;
					case ShapeType::capsule: keep(collideCapsuleBox(pair, other, probe)); break;
					case ShapeType::voxel: collideBoxVoxel(pair, probe, other, *voxel_data, manifolds); break;
				}
				break;
			case ShapeType::capsule:
				switch (shape->type) {
					case ShapeType::sphere: keep(collideSphereCapsule(pair, other, probe)); break;
					case ShapeType::box: keep(collideCapsuleBox(pair, probe, other)); break;
					case ShapeType::capsule: keep(collideCapsules(pair, probe, other)); break;
					case ShapeType::voxel: collideCapsuleVoxel(pair, probe, other, *voxel_data, manifolds); break;
				}
				break;
			case ShapeType::voxel: break;
		}

		const bool overlaps = std::ranges::any_of(manifolds, [](const Manifold& manifold) {
			const auto points = std::span {manifold.contacts}.first(manifold.contact_count);
			return std::ranges::any_of(points, [](const ContactPoint& point) { return point.penetration > 0.0f; });
		});
		if (overlaps) {
			bodies.push_back(shape->owner);
		}
	}
}

void Simulator::collideSphereProbe(
    const Body& probe_body, const Shape& probe_shape, std::span<const ShapeID> candidates, std::vector<QueryContact>& contacts
) const {
	std::vector<Manifold> manifolds;
	for (const ShapeID shape_id : candidates) {
		const Shape* shape = tryGetShape(shape_id);
		const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
		if (body == nullptr) {
			continue;
		}

		const BroadPhasePair pair {
		  .a = {},
        .b = {.body = shape->owner, .shape = shape_id}
		};
		const CollisionElement probe {probe_shape, probe_body};
		const CollisionElement other {*shape, *body};

		manifolds.clear();
		switch (shape->type) {
			case ShapeType::sphere:
				if (auto manifold = collideSpheres(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::box:
				if (auto manifold = collideSphereBox(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::capsule:
				if (auto manifold = collideSphereCapsule(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::voxel:
				if (const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data); data != nullptr && data->volume != nullptr) {
					collideSphereVoxel(pair, probe, other, *data, manifolds);
				}
				break;
		}

		for (const Manifold& manifold : manifolds) {
			if (manifold.contact_count == 0) {
				continue;
			}
			const auto points = std::span {manifold.contacts}.first(manifold.contact_count);
			const ContactPoint& deepest = *std::ranges::max_element(points, {}, &ContactPoint::penetration);
			contacts.push_back(
			    QueryContact {
			      .body = shape->owner,
			      .shape = shape_id,
			      .normal = -manifold.normal,
			      .point = deepest.position,
			      .penetration = deepest.penetration,
			    }
			);
		}
	}
}

auto Simulator::overlapCapsule(
    BodyID ignored, const CapsuleShape& capsule, const glm::vec3& position, const glm::quat& rotation, float min_penetration,
    std::vector<QueryContact>& contacts
) const -> bool {
	ZoneScopedN("physics::OverlapCapsule");
	++m_profile.character_overlap_calls;

	contacts.clear();
	const Shape probe_shape = capsuleProbe(ignored, capsule);
	const Body probe_body = probeBody(position, rotation);
	const std::vector<ShapeID> candidates = queryCandidates(ignored, worldShapeBounds(probe_body, probe_shape));
	collideCapsuleProbe(probe_body, probe_shape, candidates, contacts);
	std::erase_if(contacts, [min_penetration](const QueryContact& contact) { return contact.penetration <= min_penetration; });
	return not contacts.empty();
}

auto Simulator::overlapSphere(
    const SphereShape& sphere, const glm::vec3& position, float min_penetration, std::vector<QueryContact>& contacts
) const -> bool {
	ZoneScopedN("physics::OverlapSphere");

	contacts.clear();
	if (!std::isfinite(sphere.radius) || sphere.radius <= 0.0f || !std::isfinite(position.x) || !std::isfinite(position.y) ||
	    !std::isfinite(position.z)) {
		return false;
	}

	const Shape probe_shape = sphereProbe(sphere);
	const Body probe_body = probeBody(position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	const std::vector<ShapeID> candidates = queryCandidates({}, worldShapeBounds(probe_body, probe_shape));
	collideSphereProbe(probe_body, probe_shape, candidates, contacts);
	std::erase_if(contacts, [min_penetration](const QueryContact& contact) { return contact.penetration <= min_penetration; });
	return not contacts.empty();
}

auto Simulator::overlapAABB(const AABB& bounds) const -> bool {
	ZoneScopedN("physics::OverlapsAABB");

	const glm::vec3 size = bounds.max - bounds.min;
	if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) || !std::isfinite(bounds.min.z) ||
	    !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y) || !std::isfinite(bounds.max.z) || size.x < 0.0f ||
	    size.y < 0.0f || size.z < 0.0f) {
		return false;
	}

	const Shape probe_shape = boxProbe(bounds);
	const Body probe_body = probeBody((bounds.min + bounds.max) * 0.5f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	std::vector<Manifold> manifolds;
	for (const ShapeID shape_id : queryCandidates({}, bounds)) {
		const Shape* shape = tryGetShape(shape_id);
		const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
		if (body == nullptr) {
			continue;
		}

		const BroadPhasePair pair {
		  .a = {},
        .b = {.body = shape->owner, .shape = shape_id}
		};
		const CollisionElement probe {probe_shape, probe_body};
		const CollisionElement other {*shape, *body};

		manifolds.clear();
		switch (shape->type) {
			case ShapeType::sphere:
				if (auto manifold = collideSphereBox(pair, other, probe)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::box:
				if (auto manifold = collideBoxes(pair, probe, other)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::capsule:
				if (auto manifold = collideCapsuleBox(pair, other, probe)) {
					manifolds.push_back(*manifold);
				}
				break;
			case ShapeType::voxel:
				if (const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data); data != nullptr && data->volume != nullptr) {
					collideBoxVoxel(pair, probe, other, *data, manifolds);
				}
				break;
		}

		if (std::ranges::any_of(manifolds, [](const Manifold& manifold) { return manifold.contact_count != 0; })) {
			return true;
		}
	}
	return false;
}

auto Simulator::sweepCapsule(
    BodyID ignored, const CapsuleShape& capsule, const glm::quat& rotation, const glm::vec3& from, const glm::vec3& to,
    float skin, bool refine
) const -> SweepHit {
	ZoneScopedN("physics::SweepCapsule");
	++m_profile.character_sweep_calls;

	SweepHit result {.position = to};
	const glm::vec3 delta = to - from;
	const float distance = glm::length(delta);
	if (not std::isfinite(distance) || distance <= 1.0e-6f) {
		result.position = from;
		return result;
	}
	const glm::vec3 direction = delta / distance;

	CapsuleShape inflated = capsule;
	inflated.radius += skin;
	inflated.height += 2.0f * skin;
	const Shape probe_shape = capsuleProbe(ignored, inflated);
	Body probe_body = probeBody(from, rotation);

	const AABB start_bounds = worldShapeBounds(probe_body, probe_shape);
	const AABB end_bounds = worldShapeBounds(probeBody(to, rotation), probe_shape);
	const AABB swept_bounds = combine(start_bounds, end_bounds);
	const std::vector<ShapeID> candidates = queryCandidates(ignored, swept_bounds);
	if (candidates.empty()) {
		return result;
	}

	// every sample used to walk the voxel surface again
	// walk it once over the whole sweep unless the sweep is too long
	constexpr float k_max_gather_volume_ratio = 4.0f;
	const bool gather = boundsVolume(swept_bounds) <= k_max_gather_volume_ratio * boundsVolume(start_bounds);
	const _detail::WorldCapsule start_capsule = _detail::worldCapsule(probe_body, inflated);
	SweepScratch& scratch = sweepScratch();
	scratch.targets.clear();
	scratch.voxels.clear();
	{
		ZoneNamedN(gather_zone, "physics::SweepGather", true);
		for (const ShapeID shape_id : candidates) {
			const Shape* shape = tryGetShape(shape_id);
			const Body* body = shape != nullptr ? tryGetBody(shape->owner) : nullptr;
			if (body == nullptr) {
				continue;
			}

			SweepTarget target {.shape = shape_id, .body = shape->owner};
			if (shape->type == ShapeType::voxel) {
				const VoxelShapeData* data = tryGetVoxelDataConst(shape->voxel.data);
				if (data == nullptr || data->volume == nullptr) {
					continue;
				}

				// same frame collideCapsuleVoxel builds
				const glm::quat voxel_rotation = glm::normalize(body->rotation * shape->voxel.local_rotation);
				target.voxel_data = data;
				target.origin = body->position + body->rotation * shape->voxel.local_center;
				target.basis = glm::mat3_cast(voxel_rotation);
				const glm::mat3 basis_inv = glm::transpose(target.basis);
				target.local_a = basis_inv * (start_capsule.point_a - target.origin);
				target.local_b = basis_inv * (start_capsule.point_b - target.origin);
				target.local_delta = basis_inv * delta;

				if (gather) {
					const glm::vec3 end_a = target.local_a + target.local_delta;
					const glm::vec3 end_b = target.local_b + target.local_delta;
					const AABB local_swept {
					  .min = glm::min(glm::min(target.local_a, target.local_b), glm::min(end_a, end_b)) - glm::vec3(start_capsule.radius),
					  .max = glm::max(glm::max(target.local_a, target.local_b), glm::max(end_a, end_b)) + glm::vec3(start_capsule.radius),
					};
					const VoxelQueryContext context {
					  .volume = *data->volume, .surface = data->surface, .palette = data->palette, .materials = data->materials
					};
					target.gathered = true;
					target.first_voxel = scratch.voxels.size();
					queryVoxelSurface(context, local_swept, [&](const VoxelCandidate& candidate) {
						if (candidate.normal_index < voxel::k_normal_direction_count) {
							scratch.voxels.push_back(SweepVoxel {.min = candidate.min, .normal_index = candidate.normal_index});
						}
					});
					target.voxel_count = scratch.voxels.size() - target.first_voxel;
				}
			}
			scratch.targets.push_back(target);
		}
		ZoneValueV(gather_zone, scratch.voxels.size());    // number of surface voxel the sweep tests
	}

	const auto contacts_at = [&](float t) -> std::span<const QueryContact> {
		scratch.contacts.clear();
		probe_body.position = from + delta * t;
		for (const SweepTarget& target : scratch.targets) {
			if (target.voxel_data != nullptr) {
				voxelSweepContacts(target, scratch.voxels, start_capsule.radius, t, scratch.contacts);
			} else {
				collideCapsuleProbe(probe_body, probe_shape, std::span {&target.shape, 1}, scratch.contacts);
			}
		}
		return scratch.contacts;
	};

	const float blocking_depth = skin * 0.5f;
	const float target_depth = skin * 0.25f;
	const float accept_depth = skin * 0.125f;

	struct Sample {
		float t = 0.0f;
		std::optional<QueryContact> blocking;                      // deepest contact
		std::optional<QueryContact> touching;                      // closest to blocking
		float newton = std::numeric_limits<float>::quiet_NaN();    // guess for where the contacts reach target_depth
	};

	const auto sample = [&](float t) -> Sample {
		Sample out {.t = t};
		float back = std::numeric_limits<float>::infinity();
		float ahead = std::numeric_limits<float>::infinity();
		for (const QueryContact& contact : contacts_at(t)) {
			if (glm::dot(direction, contact.normal) >= -k_approach_epsilon) {
				continue;
			}
			const float rate = -glm::dot(delta, contact.normal);
			const float root = t + ((target_depth - contact.penetration) / rate);
			if (contact.penetration > blocking_depth) {
				back = std::min(back, root);
				if (not out.blocking.has_value() || contact.penetration > out.blocking->penetration) {
					out.blocking = contact;
				}
			} else {
				ahead = std::min(ahead, root);
				if (not out.touching.has_value() || contact.penetration > out.touching->penetration) {
					out.touching = contact;
				}
			}
		}
		if (out.blocking.has_value()) {
			out.newton = back;
		} else if (out.touching.has_value()) {
			out.newton = ahead;
		}
		return out;
	};

	// samples closer than the radius so thin geometry cannot fall between two of them
	const float sample_step = std::max(capsule.radius * 0.5f, skin) / distance;
	float clear = 0.0f;
	float blocked = 1.0f;
	Sample last;
	{
		ZoneNamedN(march_zone, "physics::SweepMarch", true);
		uint64_t samples = 0;
		for (float t = std::min(sample_step, 1.0f);; t = std::min(t + sample_step, 1.0f)) {
			++samples;
			last = sample(t);
			if (last.blocking.has_value()) {
				blocked = t;
				break;
			}
			clear = t;
			if (t >= 1.0f) {
				ZoneValueV(march_zone, samples);
				return result;
			}
		}
		ZoneValueV(march_zone, samples);
	}

	ZoneNamedN(refine_zone, "physics::SweepRefine", true);
	std::optional<QueryContact> hit = last.blocking;
	const float tolerance = skin * 0.25f / distance;
	const int refine_iterations = refine ? static_cast<int>(tunables().sweep_refine_iterations) : 0;
	uint64_t passes = 0;
	for (int iteration = 0; iteration < refine_iterations && blocked - clear > tolerance; ++iteration) {
		++passes;
		// Newton formula
		float guess = last.newton;
		if (not(guess > clear && guess < blocked)) {
			guess = 0.5f * (clear + blocked);
		}

		last = sample(guess);
		if (last.blocking.has_value()) {
			blocked = guess;
			hit = last.blocking;
			continue;
		}
		clear = guess;
		if (last.touching.has_value() && last.touching->penetration >= accept_depth) {
			// within the skin of what stops the move
			hit = last.touching;
			break;
		}
	}
	ZoneValueV(refine_zone, passes);

	return SweepHit {
	  .hit = true,
	  .fraction = clear,
	  .position = from + delta * clear,
	  .normal = hit->normal,
	  .point = hit->point,
	  .body = hit->body,
	  .shape = hit->shape,
	};
}

auto Simulator::setCapsuleShape(BodyID body, const CapsuleShape& capsule) -> bool {
	if (not mainThreadMutationAllowed()) {
		return false;
	}

	Shape* shape = nullptr;
	ShapeID shape_id;
	for (const NodeBinding& binding : m_node_bindings) {
		if (binding.body != body) {
			continue;
		}
		for (const ColliderBinding& collider : binding.colliders) {
			if (Shape* candidate = tryGetShape(collider.shape); candidate != nullptr && candidate->type == ShapeType::capsule) {
				shape = candidate;
				shape_id = collider.shape;
				break;
			}
		}
		break;
	}
	const float rotation_length_squared = glm::dot(capsule.local_rotation, capsule.local_rotation);
	const bool valid_capsule = std::isfinite(capsule.radius) && capsule.radius > 0.0f && std::isfinite(capsule.height) &&
	                           capsule.height >= 2.0f * capsule.radius && std::isfinite(rotation_length_squared) &&
	                           rotation_length_squared > 1.0e-10f;
	if (shape == nullptr || not valid_capsule) {
		return false;
	}

	shape->capsule = capsule;
	shape->capsule.local_rotation = glm::normalize(capsule.local_rotation);
	incrementShapeRevision(shape_id);
	wakeBodiesTouching(shape_id);
	return true;
}

void Simulator::moveKinematicBody(BodyID id, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& velocity) {
	if (not mainThreadMutationAllowed()) {
		return;
	}

	Body* body = tryGetBody(id);
	if (body == nullptr || body->type != BodyType::kinematic_body) {
		return;
	}

	const bool moved = body->position != position;
	body->position = position;
	body->rotation = glm::normalize(rotation);
	body->linear_velocity = velocity;
	body->angular_velocity = {};

	if (not moved) {
		return;
	}
	for (const NodeBinding& binding : m_node_bindings) {
		if (binding.body != id) {
			continue;
		}
		for (const ColliderBinding& collider : binding.colliders) {
			if (const Shape* shape = tryGetShape(collider.shape)) {
				wakeBodiesInBounds(worldShapeBounds(*body, *shape).expanded(k_kinematic_wake_margin));
			}
		}
		break;
	}
}

void Simulator::pushBody(BodyID id, const glm::vec3& point, const glm::vec3& direction, float speed, float max_impulse) {
	if (not mainThreadMutationAllowed()) {
		return;
	}

	Body* body = tryGetBody(id);
	if (body == nullptr || not body->enabled || body->type != BodyType::dynamic_body || body->inverse_mass <= 0.0f ||
	    max_impulse <= 0.0f) {
		return;
	}

	const float missing_speed = speed - glm::dot(body->linear_velocity, direction);
	if (missing_speed <= 0.0f) {
		return;
	}

	if (body->sleep_locked) {
		unlockSleep(id);
	}
	wakeBody(id);

	const glm::vec3 impulse = direction * std::min(missing_speed / body->inverse_mass, max_impulse);
	body->linear_velocity += impulse * body->inverse_mass;
	body->angular_velocity += body->inverse_inertia_world * glm::cross(point - body->worldCenterOfMass(), impulse);
}

}
