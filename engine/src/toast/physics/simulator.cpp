#include "simulator.hpp"

#include "accumulator.hpp"
#include "nodes/rigidbody.hpp"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

namespace physics {

Simulator::Simulator() {
	TOAST_ASSERT(not instance, "Physics", "Simulator can only be created once");
	TOAST_INFO("Physics", "Simulator created");
	instance = this;

	// TODO: only singlethreaded right now
	m_manifold_queues.emplace_back();
}
Simulator::~Simulator() {
	TOAST_INFO("Physics", "Simulator destroyed");
	instance = nullptr;
}

void Simulator::registerRigidbody(Rigidbody& node) {
	TOAST_ASSERT(instance, "Physics", "Simulator instance is null; cannot register rigidbody");
	if (instance->valid(node.m_body)) {
		return;
	}
	
	PhysicsMaterial material;
	if (node.material.hasValue()) {
		material.restitution = node.material->restitution();
		material.static_friction = node.material->staticFriction();
		material.dynamic_friction = node.material->dynamicFriction();
	}

	std::vector<SphereShape> spheres = node.sphereShapes();
	std::vector<BoxShape> boxes = node.boxShapes();
	std::vector<CapsuleShape> capsules = node.capsuleShapes();
	const BodyID body = instance->createBody(node.descriptor());
	node.assignBody(body);
	if (instance->valid(body)) {
		instance->m_node_bindings.push_back({.body = body, .node = node.box().as<Rigidbody>()});

		size_t registered_shape_count = 0;
		for (const auto& sphere : spheres) {
			const ShapeID shape = instance->createSphere(body, sphere, material);
			if (not instance->valid(shape)) {
				TOAST_WARN("Physics", "Sphere collider on rigidbody '{}' was not registered", node.name());
				continue;
			}
			++registered_shape_count;
		}

		for (const auto& box : boxes) {
			const ShapeID shape = instance->createBox(body, box, material);
			if (not instance->valid(shape)) {
				TOAST_WARN("Physics", "Box collider on rigidbody '{}' was not registered", node.name());
				continue;
			}
			++registered_shape_count;
		}

		for (const auto& capsule : capsules) {
			const ShapeID shape = instance->createCapsule(body, capsule, material);
			if (not instance->valid(shape)) {
				TOAST_WARN("Physics", "Capsule collider on rigidbody '{}' was not registered", node.name());
				continue;
			}
			++registered_shape_count;
		}

		if (registered_shape_count == 0) {
			TOAST_WARN("Physics", "Rigidbody '{}' registered without an enabled valid collider", node.name());
		} else {
			// All shapes exist now so we can calculate their inertia
			instance->rebuildMassProperties(body);
			TOAST_TRACE("Physics", "Registered rigidbody '{}' with {} shapes", node.name(), registered_shape_count);
		}
	}
}

void Simulator::unregisterRigidbody(Rigidbody& node) {
	if (!instance) {
		return;
	}
	const BodyID body = node.m_body;
	if (instance->valid(body)) {
		TOAST_TRACE("Physics", "Unregistering rigidbody '{}'", node.name());
	}
	instance->destroyBody(body);
	std::erase_if(instance->m_node_bindings, [body](const NodeBinding& binding) { return binding.body == body; });
	node.assignBody({});
}

void Simulator::tick() {
	ZoneScoped;

	integrate(static_cast<float>(Accumulator::fixed_delta));

	// clear every manifold queue before starting the record step
	clearManifoldQueues();

	// record manifolds into the queue
	const auto candidates = broadPhase();
	narrowPhase(candidates);

	// now sort the manifolds
	mergeManifoldQueues();
	sortManifolds();

	// resolve
	auto constraints = prepareConstraints(m_manifolds);
	solveConstraints(constraints);
	correctPositions(m_manifolds);

	// push poses after simulation settles
	publishTransforms();
}

void Simulator::publishTransforms() {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(m_node_bindings.size()));

	for (auto binding = m_node_bindings.begin(); binding != m_node_bindings.end();) {
		if (not publishTransform(*binding)) {
			binding = m_node_bindings.erase(binding);
			continue;
		}
		++binding;
	}
}

auto Simulator::publishTransform(NodeBinding& binding) -> bool {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(binding.body.slot));

	Body* body = tryGetBody(binding.body);
	if (not binding.node.exists() || not body) {
		return false;
	}

	if (body->type == BodyType::dynamic_body) {
		binding.node->applyPhysicsTransform(body->position, body->rotation);
	}
	return true;
}

auto Simulator::velocityAtPoint(const Body& body, const glm::vec3& r) -> glm::vec3 {
	return body.linear_velocity + glm::cross(body.angular_velocity, r);
}

auto Simulator::effectiveMassAlong(
    const Body& body_a, const Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction
) -> std::optional<float> {
	glm::vec3 angular_a = body_a.inverse_inertia_world * glm::cross(r_a, direction);
	glm::vec3 angular_b = body_b.inverse_inertia_world * glm::cross(r_b, direction);
	float denominator = body_a.inverse_mass + body_b.inverse_mass + glm::dot(direction, glm::cross(angular_a, r_a) + glm::cross(angular_b, r_b));

	if (not std::isfinite(denominator) || denominator <= 1.0e-8f) {
		return std::nullopt;
	}
	
	return 1.0f / denominator;
}

void Simulator::applyImpulse(Body& body_a, Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse) {
	body_a.linear_velocity -= impulse * body_a.inverse_mass;
	body_a.angular_velocity -= body_a.inverse_inertia_world * glm::cross(r_a, impulse);
	body_b.linear_velocity += impulse * body_b.inverse_mass;
	body_b.angular_velocity += body_b.inverse_inertia_world * glm::cross(r_b, impulse);
}

auto Simulator::solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool {
	glm::vec3 relative_velocity = velocityAtPoint(body_b, constraint.r_b) - velocityAtPoint(body_a, constraint.r_a);
	float normal_speed = glm::dot(relative_velocity,  constraint.normal);
	if (not std::isfinite(normal_speed)) {
		return false;
	}
	
	float impulse_delta = (constraint.restitution_bias - normal_speed) * constraint.normal_mass;
	float old_impulse = constraint.accumulated_normal_impulse;
	constraint.accumulated_normal_impulse = std::max(0.0f, old_impulse + impulse_delta);
	float applied_impulse = constraint.accumulated_normal_impulse - old_impulse;
	applyImpulse(body_a, body_b, constraint.r_a, constraint.r_b, constraint.normal * applied_impulse);
	
	return true;
}

auto Simulator::solveFriction(Constraint& constraint, Body& body_a, Body& body_b) -> bool {
	if (constraint.tangent_mass <= 0.0f) {
		return true;
	}
	
	// recalcualte because the normal impulse changed velocity
	glm::vec3 relative_velocity = velocityAtPoint(body_b, constraint.r_b) - velocityAtPoint(body_a, constraint.r_a);
	float tangent_speed = glm::dot(relative_velocity, constraint.tangent);
	if (not std::isfinite(tangent_speed)) {
		return false;
	}
	
	float impulse_delta = -tangent_speed * constraint.tangent_mass;
	float old_impulse = constraint.accumulated_tangent_impulse;
	float propsed_impulse = old_impulse + impulse_delta;
	float static_limit = constraint.static_friction * constraint.accumulated_normal_impulse;
	float new_impulse = 0.0f;
	
	if (std::abs(propsed_impulse) <= static_limit) {
		// no slipping
		new_impulse = propsed_impulse;
	} else {
		// slipping
		float dynamic_limit = constraint.dynamic_friction * constraint.accumulated_normal_impulse;
		new_impulse = std::clamp(propsed_impulse, -dynamic_limit, dynamic_limit);
	}
	
	constraint.accumulated_tangent_impulse = new_impulse;
	float applied_impulse = new_impulse - old_impulse;
	applyImpulse(body_a, body_b, constraint.r_a, constraint.r_b, constraint.tangent * applied_impulse);
	
	return true;
}

void Simulator::correctPositions(const std::vector<Manifold>& manifolds) {
	constexpr float penetration_slop = 0.005f;
	constexpr float correction_beta = 0.2f;
	constexpr float max_correction = 0.05f;
	
	for (const Manifold& manifold : manifolds) {
		auto* body_a = tryGetBody(manifold.pair.a.body);
		auto* body_b = tryGetBody(manifold.pair.b.body);
		
		if (not body_a or not body_b) {
			continue;
		}

		float deepest_penetration = 0.0f;
		const size_t contact_count = std::min<size_t>(manifold.contact_count, manifold.contacts.size());
		for (size_t contact_index = 0; contact_index < contact_count; ++contact_index) {
			float penetration = manifold.contacts[contact_index].penetration;
			if (std::isfinite(penetration) && penetration >= 0.0f) {
				deepest_penetration = std::max(deepest_penetration, penetration);
			}
		}
		
		float inv_mass = body_a->inverse_mass + body_b->inverse_mass;
		if (not std::isfinite(inv_mass) || inv_mass <= 1.0e-8f) {
			// both bodies are static
			continue;
		}
		
		// ignore tiny overlaps to prevent jitter
		float excess_penetration = std::max(deepest_penetration - penetration_slop, 0.0f);
		if (excess_penetration == 0.0f) {
			continue;
		}
		
		float correction_distance = std::min(correction_beta * excess_penetration, max_correction);
		glm::vec3 correction = manifold.normal * (correction_distance / inv_mass);
		
		body_a->position -= correction * body_a->inverse_mass;
		body_b->position += correction * body_b->inverse_mass;
	}
}

void Simulator::flipManifold(Manifold& manifold) {
	manifold.normal = -manifold.normal;
	for (size_t i = 0; i < manifold.contact_count; ++i) {
		std::swap(manifold.contacts[i].feature_a, manifold.contacts[i].feature_b);
	}
}

void Simulator::integrate(float dt) {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(m_bodies.size()));

	if (not std::isfinite(dt) or dt <= 0.0f) {
		return;
	}

	for (size_t index = 0; index < m_bodies.size(); ++index) {
		BodySlot& slot = m_bodies[index];
		if (not slot.occupied) {
			continue;
		}

		integrateBody(
		    BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation},
		    slot.body,
		    gravity,
		    dt
		);
	}
}

void Simulator::integrateBody(BodyID id, Body& body, const glm::vec3& gravity, float dt) {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(id.slot));

	// preserve the previous pose for interpolation
	body.previous_position = body.position;
	body.previous_rotation = body.rotation;

	if (body.type != BodyType::dynamic_body) {
		return;
	}

	// linear integration
	body.linear_velocity += gravity * body.gravity_scale * dt;
	body.position += body.linear_velocity * dt;

	// angular integration
	glm::quat omega_q = { 0.0f, body.angular_velocity.x, body.angular_velocity.y, body.angular_velocity.z };
	glm::quat rotation_derivative = 0.5f * omega_q * body.rotation;
	glm::quat next_rotation = body.rotation + rotation_derivative * dt;
	float length_sq = glm::dot(next_rotation, next_rotation);
	if (std::isfinite(length_sq) && length_sq > 1.0e-10f) {
		body.rotation = glm::normalize(next_rotation);
	} else {
		TOAST_WARN("Physics", "Body {} has an invalid orientation", id.slot);
		body.angular_velocity = {};
	}
	// update the inertia matrix after rotating
	glm::mat3 rot_matrix = glm::mat3_cast(body.rotation);
	body.inverse_inertia_world = rot_matrix * body.inverse_inertia_local * glm::transpose(rot_matrix);
}

void Simulator::callTick() { 
	TOAST_ASSERT(instance, "Physics", "Simulator instance is null; cannot tick");
	instance->tick();
}

auto Simulator::createBody(const BodyDescriptor& descriptor) -> BodyID {
	if (descriptor.type == BodyType::dynamic_body && (not std::isfinite(descriptor.mass) or descriptor.mass <= 0.0f)) {
		TOAST_WARN("Physics", "Rejected dynamic body with non-finite or non-positive mass");
		return {};
	}

	if (not std::isfinite(descriptor.gravity_scale)) {
		TOAST_WARN("Physics", "Rejected body with non-finite gravity scale");
		return {};
	}

	const float rotation_length_squared = glm::dot(descriptor.rotation, descriptor.rotation);
	if (not std::isfinite(rotation_length_squared) or rotation_length_squared <= 0.0f) {
		TOAST_WARN("Physics", "Rejected body with a non-finite or zero-length orientation");
		return {};
	}

	const glm::quat rotation = glm::normalize(descriptor.rotation);
	const float inverse_mass = descriptor.type == BodyType::dynamic_body ? 1.0f / descriptor.mass : 0.0f;

	Body body {
	  .type = descriptor.type,
	  .position = descriptor.position,
	  .rotation = rotation,
	  .previous_position = descriptor.position,
	  .previous_rotation = rotation,
	  .linear_velocity = descriptor.linear_velocity,
	  .angular_velocity = descriptor.angular_velocity,
	  .inverse_mass = inverse_mass,
	  .gravity_scale = descriptor.gravity_scale
	};

	if (not m_free_body_slots.empty()) {
		// try reusing dead slots to avoid allocating
		const uint32_t index = m_free_body_slots.back();
		m_free_body_slots.pop_back();

		BodySlot& slot = m_bodies[index];
		slot.body = body;
		slot.occupied = true;
		return {.slot = index, .generation = slot.generation};
	}

	// best effort :(
	// just push back and regrow if needed
	const uint32_t index = static_cast<uint32_t>(m_bodies.size());
	m_bodies.emplace_back(BodySlot {.body = body, .generation = 1, .occupied = true});
	return {.slot = index, .generation = 1};
}

void Simulator::destroyBody(BodyID body) {
	if (not valid(body)) {
		return;
	}

	for (uint32_t index = 0; index < m_shapes.size(); ++index) {
		ShapeSlot& shape_slot = m_shapes[index];
		if (shape_slot.occupied && shape_slot.shape.owner == body) {
			destroyShape({.slot = index, .generation = shape_slot.generation});
		}
	}

	BodySlot& slot = m_bodies[body.slot];
	slot.occupied = false;
	// invalidate every stale handle
	++slot.generation;
	if (slot.generation == 0) {
		++slot.generation;
	}
	m_free_body_slots.emplace_back(body.slot);
}

auto Simulator::createSphere(BodyID owner, const SphereShape& sphere, PhysicsMaterial material) -> ShapeID {
	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected sphere registration for an invalid body");
		return {};
	}

	if (not std::isfinite(sphere.radius) or sphere.radius <= 0.0f || not std::isfinite(sphere.local_center.x) or
	    not std::isfinite(sphere.local_center.y) or not std::isfinite(sphere.local_center.z)) {
		TOAST_WARN("Physics", "Rejected sphere with invalid radius or local center");
		return {};
	}

	Shape shape {
	  .owner = owner,
	  .type = ShapeType::sphere,
	  .material = material,
	  .sphere = sphere,
	};

	if (not m_free_shape_slots.empty()) {
		const uint32_t index = m_free_shape_slots.back();
		m_free_shape_slots.pop_back();

		ShapeSlot& slot = m_shapes[index];
		slot.shape = shape;
		slot.occupied = true;
		return {.slot = index, .generation = slot.generation};
	}

	const uint32_t index = static_cast<uint32_t>(m_shapes.size());
	m_shapes.emplace_back(ShapeSlot {.shape = shape, .generation = 1, .occupied = true});
	return {.slot = index, .generation = 1};
}

auto Simulator::createBox(BodyID owner, const BoxShape& box, PhysicsMaterial material) -> ShapeID {
	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected box registration for an invalid body");
		return {};
	}

	const bool center_is_finite = std::isfinite(box.local_center.x) && std::isfinite(box.local_center.y) &&
	                              std::isfinite(box.local_center.z);
	const bool size_is_valid = std::isfinite(box.size.x) && box.size.x > 0.0f && std::isfinite(box.size.y) &&
	                           box.size.y > 0.0f && std::isfinite(box.size.z) && box.size.z > 0.0f;
	const float rotation_length_squared = glm::dot(box.local_rotation, box.local_rotation);
	if (not center_is_finite || not size_is_valid || not std::isfinite(rotation_length_squared) ||
	    rotation_length_squared <= 1.0e-10f) {
		TOAST_WARN("Physics", "Rejected box with invalid size, local center, or local rotation");
		return {};
	}

	BoxShape normalized_box = box;
	normalized_box.local_rotation = glm::normalize(box.local_rotation);

	Shape shape {
	  .owner = owner,
	  .type = ShapeType::box,
	  .material = material,
	  .box = normalized_box,
	};

	if (not m_free_shape_slots.empty()) {
		const uint32_t index = m_free_shape_slots.back();
		m_free_shape_slots.pop_back();

		ShapeSlot& slot = m_shapes[index];
		slot.shape = shape;
		slot.occupied = true;
		return {.slot = index, .generation = slot.generation};
	}

	const uint32_t index = static_cast<uint32_t>(m_shapes.size());
	m_shapes.emplace_back(ShapeSlot {.shape = shape, .generation = 1, .occupied = true});
	return {.slot = index, .generation = 1};
}

auto Simulator::createCapsule(BodyID owner, const CapsuleShape& capsule, PhysicsMaterial material) -> ShapeID {
	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected capsule registration for an invalid body");
		return {};
	}

	const bool center_is_finite = std::isfinite(capsule.local_center.x) && std::isfinite(capsule.local_center.y) &&
	                              std::isfinite(capsule.local_center.z);
	const bool dimensions_are_valid = std::isfinite(capsule.radius) && capsule.radius > 0.0f &&
	                                  std::isfinite(capsule.height) && capsule.height >= 2.0f * capsule.radius;
	const float rotation_length_squared = glm::dot(capsule.local_rotation, capsule.local_rotation);
	if (not center_is_finite || not dimensions_are_valid || not std::isfinite(rotation_length_squared) ||
	    rotation_length_squared <= 1.0e-10f) {
		TOAST_WARN("Physics", "Rejected capsule with invalid dimensions, local center, or local rotation");
		return {};
	}

	CapsuleShape normalized_capsule = capsule;
	normalized_capsule.local_rotation = glm::normalize(capsule.local_rotation);

	Shape shape {
	  .owner = owner,
	  .type = ShapeType::capsule,
	  .material = material,
	  .capsule = normalized_capsule,
	};

	if (not m_free_shape_slots.empty()) {
		const uint32_t index = m_free_shape_slots.back();
		m_free_shape_slots.pop_back();

		ShapeSlot& slot = m_shapes[index];
		slot.shape = shape;
		slot.occupied = true;
		return {.slot = index, .generation = slot.generation};
	}

	const uint32_t index = static_cast<uint32_t>(m_shapes.size());
	m_shapes.emplace_back(ShapeSlot {.shape = shape, .generation = 1, .occupied = true});
	return {.slot = index, .generation = 1};
}

void Simulator::destroyShape(ShapeID shape) {
	if (not valid(shape)) {
		return;
	}

	ShapeSlot& slot = m_shapes[shape.slot];
	slot.occupied = false;
	++slot.generation;
	if (slot.generation == 0) {
		++slot.generation;
	}
	m_free_shape_slots.emplace_back(shape.slot);
}

auto Simulator::valid(ShapeID shape) const -> bool {
	return shape.slot < m_shapes.size() && m_shapes[shape.slot].occupied && m_shapes[shape.slot].generation == shape.generation;
}

auto Simulator::tryGetShape(ShapeID shape) -> Shape* {
	return valid(shape) ? &m_shapes[shape.slot].shape : nullptr;
}

auto Simulator::tryGetShape(ShapeID shape) const -> const Shape* {
	return valid(shape) ? &m_shapes[shape.slot].shape : nullptr;
}

void Simulator::rebuildMassProperties(BodyID id) {
	auto* body = tryGetBody(id);
	if (not body) {
		TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid body");
		return;
	}

	body->inverse_inertia_local = {0.0f};
	body->inverse_inertia_world = {0.0f};
	if (body->inverse_mass == 0.0f) {
		// static and kinematic bodies we just set inertia to 0
		// this is not a sanity check
		return;
	}

	std::vector<ShapeID> shapes;
	for (const auto& [index, slot] : m_shapes | std::views::enumerate) {
		if (not slot.occupied) { continue; }
		if (slot.shape.owner != id) { continue; }
		shapes.emplace_back(ShapeID{
			.slot = static_cast<uint32_t>(index),
			.generation = slot.generation,
		});
	}

	if (shapes.empty()) {
		TOAST_WARN("Physics", "rebuildMassProperties() was aborted: no colliders");
		return;
	}

	// TODO: Add support to multishapes
	// right now just pick the first one
	const auto* shape = tryGetShape(shapes[0]);
	if (not shape) {
		TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid shape");
		return;
	}

	switch (shape->type) {
		case ShapeType::sphere: {
			float radius_sq = shape->sphere.radius * shape->sphere.radius;
			if (not std::isfinite(radius_sq) || radius_sq <= 0.0f) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid sphere inertia denominator");
				return;
			}

			// I = 2/5 m r2
			// inv(I) = 5/(2 m r2)
			float inverse_inertia = 2.5f * body->inverse_mass / radius_sq;
			body->inverse_inertia_local = {inverse_inertia};
			glm::mat3 rotation = glm::mat3_cast(body->rotation);
			body->inverse_inertia_world = rotation * body->inverse_inertia_local * glm::transpose(rotation);
			break;
		}
		case ShapeType::box: {
			float width_sq = shape->box.size.x * shape->box.size.x;
			float height_sq = shape->box.size.y * shape->box.size.y;
			float depth_sq = shape->box.size.z * shape->box.size.z;
			float denominator_x = height_sq + depth_sq;
			float denominator_y = width_sq + depth_sq;
			float denominator_z = width_sq + height_sq;
			if (not std::isfinite(denominator_x) || denominator_x <= 0.0f || not std::isfinite(denominator_y) ||
			    denominator_y <= 0.0f || not std::isfinite(denominator_z) || denominator_z <= 0.0f) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid box inertia denominator");
				return;
			}

			// Ix = 1/12 m (h2 + d2)
			// inv(Ix) = 12/(m (h2 + d2))
			float inverse_inertia_x = 12.0f * body->inverse_mass / denominator_x;
			// Iy = 1/12 m (w2 + d2)
			// inv(Iy) = 12/(m (w2 + d2))
			float inverse_inertia_y = 12.0f * body->inverse_mass / denominator_y;
			// Iz = 1/12 m (w2 + h2)
			// inv(Iz) = 12/(m (w2 + h2))
			float inverse_inertia_z = 12.0f * body->inverse_mass / denominator_z;

			glm::mat3 inverse_inertia = {0.0f};
			inverse_inertia[0][0] = inverse_inertia_x;
			inverse_inertia[1][1] = inverse_inertia_y;
			inverse_inertia[2][2] = inverse_inertia_z;
			glm::mat3 local_rotation = glm::mat3_cast(shape->box.local_rotation);
			body->inverse_inertia_local = local_rotation * inverse_inertia * glm::transpose(local_rotation);
			glm::mat3 rotation = glm::mat3_cast(body->rotation);
			body->inverse_inertia_world = rotation * body->inverse_inertia_local * glm::transpose(rotation);
			break;
		}
		case ShapeType::capsule: {
			// Conservative approximation: use a box with dimensions (2r, 2r, height)
			float diameter = 2.0f * shape->capsule.radius;
			float diameter_sq = diameter * diameter;
			float height_sq = shape->capsule.height * shape->capsule.height;
			float denominator_xy = diameter_sq + height_sq;
			float denominator_z = diameter_sq + diameter_sq;
			if (not std::isfinite(denominator_xy) || denominator_xy <= 0.0f || not std::isfinite(denominator_z) ||
			    denominator_z <= 0.0f) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid capsule inertia denominator");
				return;
			}

			// Ix = 1/12 m (d2 + h2)
			// inv(Ix) = 12/(m (d2 + h2))
			float inverse_inertia_x = 12.0f * body->inverse_mass / denominator_xy;
			// Iy = 1/12 m (d2 + h2)
			// inv(Iy) = 12/(m (d2 + h2))
			float inverse_inertia_y = 12.0f * body->inverse_mass / denominator_xy;
			// Iz = 1/12 m (d2 + d2)
			// inv(Iz) = 12/(m (d2 + d2))
			float inverse_inertia_z = 12.0f * body->inverse_mass / denominator_z;

			glm::mat3 inverse_inertia = {0.0f};
			inverse_inertia[0][0] = inverse_inertia_x;
			inverse_inertia[1][1] = inverse_inertia_y;
			inverse_inertia[2][2] = inverse_inertia_z;
			glm::mat3 local_rotation = glm::mat3_cast(shape->capsule.local_rotation);
			body->inverse_inertia_local = local_rotation * inverse_inertia * glm::transpose(local_rotation);
			glm::mat3 rotation = glm::mat3_cast(body->rotation);
			body->inverse_inertia_world = rotation * body->inverse_inertia_local * glm::transpose(rotation);
			break;
		}
		default: {
			TOAST_WARN("Physics", "rebuildMassProperties() was aborted: unknown shape");
		}
	}
}

auto Simulator::valid(BodyID body) const -> bool {
	return body.slot < m_bodies.size() && m_bodies[body.slot].occupied && m_bodies[body.slot].generation == body.generation;
}

auto Simulator::state(BodyID body) const -> std::optional<BodyState> {
	const Body* value = tryGetBody(body);
	if (not value) {
		return std::nullopt;
	}

	return BodyState {
	  .type = value->type,
	  .position = value->position,
	  .rotation = value->rotation,
	  .previous_position = value->previous_position,
	  .previous_rotation = value->previous_rotation,
	  .linear_velocity = value->linear_velocity,
	  .angular_velocity = value->angular_velocity
	};
}

auto Simulator::setTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation) -> bool {
	Body* value = tryGetBody(body);
	const float rotation_length_squared = glm::dot(rotation, rotation);
	if (not value or not std::isfinite(rotation_length_squared) or rotation_length_squared <= 0.0f) {
		return false;
	}

	const glm::quat normalized_rotation = glm::normalize(rotation);
	value->position = position;
	value->rotation = normalized_rotation;
	value->previous_position = position;
	value->previous_rotation = normalized_rotation;
	return true;
}

auto Simulator::setLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool {
	Body* value = tryGetBody(body);
	if (not value) {
		return false;
	}

	value->linear_velocity = velocity;
	return true;
}

auto Simulator::tryGetBody(BodyID body) -> Body* {
	return valid(body) ? &m_bodies[body.slot].body : nullptr;
}

auto Simulator::tryGetBody(BodyID body) const -> const Body* {
	return valid(body) ? &m_bodies[body.slot].body : nullptr;
}

auto Simulator::broadPhase() const -> std::vector<BroadPhasePair> {
	ZoneScoped;

	std::vector<BroadPhasePair> pairs;

	for (size_t i = 0; i < m_shapes.size(); ++i) {
		if (not m_shapes[i].occupied) {
			continue;
		}

		for (size_t j = i + 1; j < m_shapes.size(); ++j) {
			if (auto pair = broadPhasePair(i, j)) {
				pairs.emplace_back(*pair);
			}
		}
	}

	ZoneValue(static_cast<uint64_t>(pairs.size()));
	return pairs;
}

auto Simulator::broadPhasePair(size_t shape_a_index, size_t shape_b_index) const -> std::optional<BroadPhasePair> {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(shape_a_index) << 32) | static_cast<uint64_t>(shape_b_index));

	const ShapeSlot& shape_a = m_shapes[shape_a_index];
	const ShapeSlot& shape_b = m_shapes[shape_b_index];
	if (not shape_b.occupied) {
		return std::nullopt;
	}

	// skip if they have the same owner
	if (shape_b.shape.owner == shape_a.shape.owner) {
		return std::nullopt;
	}

	const Body* body_a = tryGetBody(shape_a.shape.owner);
	const Body* body_b = tryGetBody(shape_b.shape.owner);

	// skip if body is invalid
	if (not body_a || not body_b) {
		return std::nullopt;
	}

	// skip if they are both static/kinematic
	if (body_a->inverse_mass == 0.0f && body_b->inverse_mass == 0.0f) {
		return std::nullopt;
	}

	// clang-format off
	return canonicalPair(
		BodyShapeKey {
			.body = shape_a.shape.owner,
			.shape = {
				.slot = static_cast<uint32_t>(shape_a_index),
				.generation = shape_a.generation
			}
		},
		BodyShapeKey {
			.body = shape_b.shape.owner,
			.shape = {
				.slot = static_cast<uint32_t>(shape_b_index),
				.generation = shape_b.generation
			}
		}
	);
	// clang-format on
}

void Simulator::clearManifoldQueues() {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(m_manifold_queues.size()));

	std::ranges::for_each(m_manifold_queues, [](ManifoldQueue& queue) { queue.clear(); });
}

void Simulator::narrowPhase(const std::vector<BroadPhasePair>& candidates) {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(candidates.size()));

	std::ranges::for_each(candidates, [&](auto pair) { collide(pair); });
}

void Simulator::mergeManifoldQueues() {
	ZoneScoped;

	m_manifolds.clear();

	for (const ManifoldQueue& queue : m_manifold_queues) {
		for (const Manifold& manifold : queue) {
			mergeManifold(manifold);
		}
	}

	ZoneValue(static_cast<uint64_t>(m_manifolds.size()));
}

void Simulator::mergeManifold(const Manifold& manifold) {
	ZoneScoped;
	ZoneValue(
	    (static_cast<uint64_t>(manifold.pair.a.shape.slot) << 32) |
	    static_cast<uint64_t>(manifold.pair.b.shape.slot)
	);

	m_manifolds.emplace_back(manifold);
}

void Simulator::sortManifolds() {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(m_manifolds.size()));

	std::ranges::sort(m_manifolds, [](const Manifold& lhs, const Manifold& rhs) {
		return lhs.pair < rhs.pair;
	});
}

auto Simulator::validateManifold(Manifold& manifold) const -> bool {
	const Body* body_a = tryGetBody(manifold.pair.a.body);
	const Body* body_b = tryGetBody(manifold.pair.b.body);
	const Shape* shape_a = tryGetShape(manifold.pair.a.shape);
	const Shape* shape_b = tryGetShape(manifold.pair.b.shape);
	if (!body_a || !body_b || !shape_a || !shape_b ||
	    shape_a->owner != manifold.pair.a.body || shape_b->owner != manifold.pair.b.body) {
		return false;
	}

	if (manifold.contact_count == 0 || manifold.contact_count > manifold.contacts.size()) {
		return false;
	}

	const bool normal_is_finite = std::isfinite(manifold.normal.x) &&
	                              std::isfinite(manifold.normal.y) &&
	                              std::isfinite(manifold.normal.z);
	const float normal_length_squared = glm::dot(manifold.normal, manifold.normal);
	if (!normal_is_finite || !std::isfinite(normal_length_squared) ||
	    std::abs(normal_length_squared - 1.0f) > _detail::unit_normal_tolerance) {
		return false;
	}

	for (size_t index = 0; index < manifold.contact_count; ++index) {
		ContactPoint& contact = manifold.contacts[index];
		const bool position_is_finite = std::isfinite(contact.position.x) &&
		                                std::isfinite(contact.position.y) &&
		                                std::isfinite(contact.position.z);
		if (!position_is_finite || !std::isfinite(contact.penetration) ||
		    contact.penetration < -_detail::contact_tolerance) {
			return false;
		}
		contact.penetration = std::max(contact.penetration, 0.0f);
	}

	return true;
}

auto Simulator::prepareConstraints(const std::vector<Manifold>& manifolds) const -> std::vector<Constraint> {
	ZoneScoped;
	std::vector<Constraint> constraints;
	size_t contact_count = 0;
	for (const Manifold& manifold : manifolds) {
		contact_count += std::min<size_t>(manifold.contact_count, manifold.contacts.size());
	}
	constraints.reserve(contact_count);

	size_t rejected_contact_count = 0;

	for (const Manifold& manifold : manifolds) {
		const size_t valid_contact_count = std::min<size_t>(manifold.contact_count, manifold.contacts.size());
		for (size_t contact_index = 0; contact_index < valid_contact_count; ++contact_index) {
			if (auto constraint = prepareConstraint(manifold, manifold.contacts[contact_index])) {
				constraints.emplace_back(*constraint);
			} else {
				++rejected_contact_count;
			}
		}
	}

	if (rejected_contact_count > 0) {
		TOAST_WARN("Physics", "Rejected {} invalid contact(s) while preparing constraints", rejected_contact_count);
	}

	ZoneValue(static_cast<uint64_t>(constraints.size()));
	return constraints;
}

auto Simulator::prepareConstraint(const Manifold& manifold, const ContactPoint& contact) const -> std::optional<Constraint> {
	ZoneScoped;
	ZoneValue(
	    (static_cast<uint64_t>(manifold.pair.a.body.slot) << 32) |
	    static_cast<uint64_t>(manifold.pair.b.body.slot)
	);

	const Body* body_a = tryGetBody(manifold.pair.a.body);
	const Body* body_b = tryGetBody(manifold.pair.b.body);
	if (not body_a || not body_b) {
		return std::nullopt;
	}

	const bool normal_is_finite = std::isfinite(manifold.normal.x) && std::isfinite(manifold.normal.y) &&
	                              std::isfinite(manifold.normal.z);
	const bool contact_is_finite = std::isfinite(contact.position.x) && std::isfinite(contact.position.y) &&
	                               std::isfinite(contact.position.z);
	if (not normal_is_finite || not contact_is_finite || not std::isfinite(contact.penetration) ||
	    contact.penetration < 0.0f) {
		return std::nullopt;
	}

	const glm::vec3 r_a = contact.position - body_a->position;
	const glm::vec3 r_b = contact.position - body_b->position;
	const auto normal_mass = effectiveMassAlong(*body_a, *body_b, r_a, r_b, manifold.normal);
	if (not normal_mass) {
		return std::nullopt;
	}
	const glm::vec3 relative_velocity = velocityAtPoint(*body_b, r_b) - velocityAtPoint(*body_a, r_a);
	const float initial_normal_speed = glm::dot(relative_velocity, manifold.normal);
	constexpr float restitution = 0.5f;
	constexpr float bounce_threshold = 1.0f;
	float restitution_bias = 0.0f;
	if (initial_normal_speed < -bounce_threshold) {
		restitution_bias = -restitution * initial_normal_speed;
	}
	
	const glm::vec3 tangent_velocity = relative_velocity - manifold.normal * initial_normal_speed;
	const float tangent_length_sq = glm::dot(tangent_velocity, tangent_velocity);
	glm::vec3 tangent = {};
	float tangent_mass = 0.0f;
	
	if (tangent_length_sq > 1.0e-10f) {
		tangent = tangent_velocity/sqrt(tangent_length_sq);
		const auto calculated_tangent_mass = effectiveMassAlong(*body_a, *body_b, r_a, r_b, tangent);
		if (calculated_tangent_mass.has_value()) {
			tangent_mass = calculated_tangent_mass.value();
		}
	}

	return Constraint {
	  .body_a = manifold.pair.a.body,
	  .body_b = manifold.pair.b.body,
	  .contact_point = contact.position,
	  .normal = manifold.normal,
		.tangent = tangent,
	  .r_a = r_a,
	  .r_b = r_b,
	  .penetration = contact.penetration,
		.normal_mass = normal_mass.value_or(0.0f),
	  .tangent_mass = tangent_mass,
		.restitution_bias = restitution_bias,
		.static_friction = 0.6f,
		.dynamic_friction = 0.4f,
	};
}

void Simulator::solveConstraints(std::vector<Constraint>& constraints) {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(constraints.size()));
	constexpr uint32_t solver_iterations = 8; // try 4 or 16
	size_t invalid_constraint_count = 0;

	for (uint32_t iteration = 0; iteration < solver_iterations; ++iteration) {
		ZoneScopedN("Simulator::solveConstraints()::iteration#%i");
		ZoneValue(static_cast<uint64_t>(iteration));
		for (Constraint& constraint : constraints) {
			if (not solveConstraint(constraint)) {
				++invalid_constraint_count;
			}
		}
	}

	if (invalid_constraint_count > 0) {
		TOAST_WARN("Physics", "Skipped {} invalid solver constraint(s)", invalid_constraint_count);
	}
}

auto Simulator::solveConstraint(Constraint& constraint) -> bool {
	ZoneScoped;

	Body* body_a = tryGetBody(constraint.body_a);
	Body* body_b = tryGetBody(constraint.body_b);

	if (!body_a || !body_b) {
		return false;
	}

	if (!solveNormal(constraint,*body_a,*body_b)) {
		return false;
	}

	return solveFriction(constraint, *body_a, *body_b);
}

void Simulator::collide(BroadPhasePair pair) {
	ZoneScoped;
	ZoneValue((static_cast<uint64_t>(pair.a.shape.slot) << 32) | static_cast<uint64_t>(pair.b.shape.slot));

	const Shape* shape_a = tryGetShape(pair.a.shape);
	const Shape* shape_b = tryGetShape(pair.b.shape);
	const Body* body_a = tryGetBody(pair.a.body);
	const Body* body_b = tryGetBody(pair.b.body);
	if (!shape_a || !shape_b || !body_a || !body_b ||
	    shape_a->owner != pair.a.body || shape_b->owner != pair.b.body) {
		return;
	}

#ifdef DEBUG
	// this is checked on broad phase so we don't need to check it back here
	TOAST_ASSERT(shape_a && shape_b && body_a && body_b, "Physics", "Broad-phase pair became invalid");
	TOAST_ASSERT(
			shape_a->owner == pair.a.body && shape_b->owner == pair.b.body,
			"Physics",
			"Broad-phase shape ownership mismatch"
	);
#endif

	// manifolds will be generated by the collideX functions
	// just call the correct one here
	std::optional<Manifold> manifold;
	switch (shape_a->type) {
		case ShapeType::sphere:
			switch (shape_b->type) {
				case ShapeType::sphere:
					// sphere-sphere
					manifold = collideSpheres(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
				case ShapeType::box:
					// sphere-box
					manifold = collideSphereBox(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
				case ShapeType::capsule:
					// sphere-capsule
					manifold = collideSphereCapsule(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
			}
			break;
		case ShapeType::box:
			switch (shape_b->type) {
				case ShapeType::sphere:
					// sphere-box (inverted)
					manifold = collideSphereBox(pair, *shape_b, *body_b, *shape_a, *body_a);
					if (manifold.has_value()) {
						flipManifold(*manifold);
					}
					break;
				case ShapeType::box:
					// box-box
					manifold = collideBoxes(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
				case ShapeType::capsule:
					// capsule-box (inverted)
					manifold = collideCapsuleBox(pair, *shape_b, *body_b, *shape_a, *body_a);
					if (manifold.has_value()) {
						flipManifold(*manifold);
					}
					break;
			}
			break;
		case ShapeType::capsule:
			switch (shape_b->type) {
				case ShapeType::sphere:
					// sphere-capsule (inverted)
					manifold = collideSphereCapsule(pair, *shape_b, *body_b, *shape_a, *body_a);
					if (manifold.has_value()) {
						flipManifold(*manifold);
					}
					break;
				case ShapeType::box:
					// capsule-box
					manifold = collideCapsuleBox(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
				case ShapeType::capsule:
					// capsule-capsule
					manifold = collideCapsules(pair, *shape_a, *body_a, *shape_b, *body_b);
					break;
			}
			break;
	}

	if (manifold.has_value() && validateManifold(*manifold)) {
		m_manifold_queues[0].emplace_back(*manifold);
	}
}

}
