#include "simulator.hpp"

#include "accumulator.hpp"
#include "contact_events.hpp"
#include "nodes/box_collider.hpp"
#include "nodes/capsule_collider.hpp"
#include "nodes/collider.hpp"
#include "nodes/dynamic_rigidbody.hpp"
#include "nodes/rigidbody.hpp"
#include "nodes/sphere_collider.hpp"
#include "toast/world/voxel_node.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <toast/assets/voxel_model.hpp>
#include <toast/thread_pool.hpp>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {
constexpr float sleep_linear_threshold_squared = 0.05f * 0.05f;
constexpr float sleep_angular_threshold_squared = 0.05f * 0.05f;
constexpr float sleep_delay = 0.5f;
}

Simulator::PhaseScope::PhaseScope(Simulator& simulator, SimulationPhase expected, SimulationPhase next)
    : m_simulator(simulator),
      m_previous(expected),
      m_active(next) {
	const SimulationPhase current = m_simulator.m_phase.load(std::memory_order_relaxed);
	TOAST_ASSERT(current == expected, "Physics", "Invalid physics simulation phase transition");
	m_previous = current;
	m_simulator.m_phase.store(next, std::memory_order_relaxed);
}

Simulator::PhaseScope::~PhaseScope() {
	TOAST_ASSERT(
	    m_simulator.m_phase.load(std::memory_order_relaxed) == m_active,
	    "Physics",
	    "Physics simulation phase changed while a phase scope was active"
	);
	m_simulator.m_phase.store(m_previous, std::memory_order_relaxed);
}

Simulator::Simulator() {
	TOAST_ASSERT(not instance, "Physics", "Simulator can only be created once");
	TOAST_INFO("Physics", "Simulator created");
	instance = this;
}

Simulator::~Simulator() {
	TOAST_INFO("Physics", "Simulator destroyed");
	instance = nullptr;
}

void Simulator::registerRigidbody(Rigidbody& node) {
	ZoneScopedN("physics::RegisterRigidbody");

	TOAST_ASSERT(instance, "Physics", "Simulator instance is null; cannot register rigidbody");
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}
	if (instance->valid(node.m_body)) {
		return;
	}

	PhysicsMaterial material;
	if (node.material.hasValue()) {
		material.restitution = node.material->restitution();
		material.static_friction = node.material->staticFriction();
		material.dynamic_friction = node.material->dynamicFriction();
	}

	const BodyID body = instance->createBody(node.descriptor());
	node.assignBody(body);
	if (instance->valid(body)) {
		setBodyEnabled(body, node.enabled());
		NodeBinding binding {.body = body, .node = node.box().as<Rigidbody>()};

		size_t registered_shape_count = 0;
		for (const auto& child : node.children()) {
			ShapeID shape;
			toast::Box<Collider> collider;

			if (const auto sphere = child.as<SphereCollider>(); sphere.exists()) {
				shape = instance->createSphere(body, SphereShape {.local_center = sphere->position, .radius = sphere->radius}, material);
				collider = sphere;
			} else if (const auto box = child.as<BoxCollider>(); box.exists()) {
				shape = instance->createBox(
				    body, BoxShape {.local_center = box->position, .local_rotation = box->rotation, .size = box->size}, material
				);
				collider = box;
			} else if (const auto capsule = child.as<CapsuleCollider>(); capsule.exists()) {
				shape = instance->createCapsule(
				    body,
				    CapsuleShape {
				      .local_center = capsule->position,
				      .local_rotation = capsule->rotation,
				      .radius = capsule->radius,
				      .height = capsule->height,
				    },
				    material
				);
				collider = capsule;
			} else {
				continue;
			}

			if (not instance->valid(shape)) {
				TOAST_WARN("Physics", "Invalid collider on rigidbody '{}' was not registered", node.name());
				continue;
			}

			collider->assignShape(shape);
			setShapeEnabled(shape, node.enabled() && collider->enabled() && not collider->disabled);
			binding.colliders.push_back({.shape = shape, .node = std::move(collider)});
			wakeBody(body);
			++registered_shape_count;
		}

		instance->m_node_bindings.emplace_back(std::move(binding));

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
	ZoneScopedN("physics::UnregisterRigidbody");

	if (!instance) {
		return;
	}
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}
	const BodyID body = node.m_body;
	if (instance->valid(body)) {
		TOAST_TRACE("Physics", "Unregistering rigidbody '{}'", node.name());
	}
	for (NodeBinding& binding : instance->m_node_bindings) {
		if (binding.body != body) {
			continue;
		}
		for (ColliderBinding& collider : binding.colliders) {
			if (collider.node.exists()) {
				collider.node->assignShape({});
			}
		}
	}
	instance->destroyBody(body);
	std::erase_if(instance->m_node_bindings, [body](const NodeBinding& binding) { return binding.body == body; });
	node.assignBody({});
}

void Simulator::registerVoxelNode(toast::VoxelNode& node) {
	ZoneScopedN("physics::RegisterVoxelNode");
	if (not instance) {
		return;
	}
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}
	if (std::ranges::any_of(instance->m_voxel_bindings, [&node](const VoxelNodeBinding& binding) {
		    return binding.node.exists() && &*binding.node == &node;
	    })) {
		return;
	}

	// TODO: THIS IS SEVERILY HARDCODED
	PhysicsMaterial material;
	// if (node.material.hasValue()) {
	// 	material.restitution = node.material->restitution();
	// 	material.static_friction = node.material->staticFriction();
	// 	material.dynamic_friction = node.material->dynamicFriction();
	// }

	node.syncTransform();
	const bool dynamic_body = not node.indestructible;
	const BodyID body = instance->createBody(
	    BodyDescriptor {
	      .type = dynamic_body ? BodyType::dynamic_body : BodyType::static_body,
	      .allow_sleep = node.allow_sleep,
	      .position = node.world_position,
	      .rotation = node.world_rotation,
	      .mass = 1.0f,    // HACK: HARDCODED
	      .gravity_scale = node.gravity_scale,
	    }
	);
	if (not instance->valid(body)) {
		return;
	}

	const ShapeID shape = instance->createVoxelShape(body, node);
	if (not instance->valid(shape)) {
		instance->destroyBody(body);
		return;
	}
	if (Shape* stored_shape = instance->tryGetShape(shape)) {
		stored_shape->material = material;
	}

	node.assignBody(body);
	node.assignShape(shape);
	setBodyEnabled(body, node.enabled());
	setShapeEnabled(shape, node.enabled());
	instance->m_voxel_bindings.push_back({
	  .body = body,
	  .shape = shape,
	  .node = node.box().as<toast::VoxelNode>(),
	  .source_revision = node.revision(),
	  .source_model = node.getModel().uid().data(),
	  .source_palette = node.paletteUid(),
	});

	if (dynamic_body) {
		instance->rebuildMassProperties(body);
	}
}

void Simulator::unregisterVoxelNode(toast::VoxelNode& node) {
	ZoneScopedN("physics::UnregisterVoxelNode");
	if (not instance) {
		return;
	}
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}

	for (const VoxelNodeBinding& binding : instance->m_voxel_bindings) {
		if (binding.node.exists() && &*binding.node == &node) {
			instance->destroyBody(binding.body);
		}
	}
	std::erase_if(instance->m_voxel_bindings, [&node](const VoxelNodeBinding& binding) {
		return binding.node.exists() && &*binding.node == &node;
	});
	node.assignBody({});
	node.assignShape({});
}

auto Simulator::nodeFor(BodyID body) -> toast::Box<toast::Node> {
	if (not instance) {
		return {};
	}

	const auto rigidbody_binding =
	    std::ranges::find_if(instance->m_node_bindings, [body](const NodeBinding& candidate) { return candidate.body == body; });
	if (rigidbody_binding != instance->m_node_bindings.end()) {
		return rigidbody_binding->node;
	}

	const auto voxel_binding = std::ranges::find_if(instance->m_voxel_bindings, [body](const VoxelNodeBinding& candidate) {
		return candidate.body == body;
	});
	if (voxel_binding != instance->m_voxel_bindings.end()) {
		return voxel_binding->node;
	}

	return {};
}

auto Simulator::mainThreadMutationAllowed() const -> bool {
	const SimulationPhase phase = m_phase.load(std::memory_order_relaxed);
	if (phase == SimulationPhase::idle) {
		// No step is in flight, so whichever thread calls now becomes the designated mutation
		// thread until the next step starts. This lets the editor build/load workspaces on its
		// UI thread (tick loop paused) and still catches a worker thread mutating physics state
		// while a step owned by a different thread is actually running.
		m_owner_thread = std::this_thread::get_id();
	}
	const bool is_owner_thread = std::this_thread::get_id() == m_owner_thread;
	const bool workers_are_idle = phase != SimulationPhase::worker_execution;
	TOAST_ASSERT(is_owner_thread, "Physics", "Physics-owned data may only be mutated from the simulator thread");
	TOAST_ASSERT(workers_are_idle, "Physics", "Physics-owned data may not be mutated while worker jobs are executing");
	return is_owner_thread && workers_are_idle;
}

void Simulator::tick() {
	ZoneScopedN("physics::Step");
	if (not mainThreadMutationAllowed()) {
		return;
	}
	PhaseScope step_phase {*this, SimulationPhase::idle, SimulationPhase::mutation};
	m_profile = {};

	const float dt = static_cast<float>(Accumulator::fixed_delta);
	syncEnabledState();
	integrate(dt);

	{
		PhaseScope worker_phase {*this, SimulationPhase::mutation, SimulationPhase::worker_execution};
		const CollisionWorldView world {.bodies = m_bodies, .shapes = m_shapes, .voxel_shapes = m_voxel_shapes};
		const auto candidates = m_broad_phase.findPairs(world);
		m_manifolds = generateManifoldsAsync(world, candidates);
	}
	updateCache(m_manifolds);
	wakeContactGroups();

	// resolve
	auto constraints = prepareConstraints(m_manifolds);
	auto islands = buildIslands(m_manifolds, constraints);
	solveIslands(islands);
	updateSleeping(dt);
	publishProfile(islands);

	// push poses after simulation settles
	publishTransforms();
	publishVoxelRenderRecords();
	FrameMarkNamed("PhysicsStep");
}

void Simulator::publishProfile(std::span<const SimulationIsland> islands) const {
	ZoneScopedN("physics::PublishProfile");
	(void)islands;
}

void Simulator::syncEnabledState() {
	ZoneScopedN("physics::SyncEnabledState");

	for (NodeBinding& binding : m_node_bindings) {
		Body* body = tryGetBody(binding.body);
		if (not body) {
			continue;
		}

		body->enabled = binding.node.exists() && binding.node->enabled();
		if (const auto dynamic_node = binding.node.as<DynamicRigidbody>(); dynamic_node.exists()) {
			body->allow_sleep = dynamic_node->allow_sleep;
			if (not body->allow_sleep) {
				wakeBody(binding.body);
			}
		}
		for (ColliderBinding& collider_binding : binding.colliders) {
			Shape* shape = tryGetShape(collider_binding.shape);
			if (not shape) {
				continue;
			}

			const bool enabled = body->enabled && collider_binding.node.exists() && collider_binding.node->enabled() &&
			                     not collider_binding.node->disabled;
			if (shape->enabled != enabled) {
				shape->enabled = enabled;
				incrementShapeRevision(collider_binding.shape);
			}
		}
	}

	std::vector<toast::Box<toast::VoxelNode>> voxel_nodes_to_reregister;

	for (VoxelNodeBinding& binding : m_voxel_bindings) {
		Body* body = tryGetBody(binding.body);
		if (body == nullptr || not binding.node.exists()) {
			continue;
		}

		const bool wants_dynamic = not binding.node->indestructible;
		if (wants_dynamic != (body->type == BodyType::dynamic_body)) {
			voxel_nodes_to_reregister.push_back(binding.node);
			continue;
		}

		if (body->type == BodyType::dynamic_body) {
			body->allow_sleep = binding.node->allow_sleep;
			if (not body->allow_sleep) {
				wakeBody(binding.body);
			}
		} else {
			binding.node->syncTransform();
			const bool position_changed =
			    glm::any(glm::greaterThan(glm::abs(body->position - binding.node->world_position), glm::vec3(1.0e-5f)));
			const bool rotation_changed = 1.0f - std::abs(glm::dot(body->rotation, binding.node->world_rotation)) > 1.0e-5f;
			if (position_changed || rotation_changed) {
				setTransform(binding.body, binding.node->world_position, binding.node->world_rotation);
				body = tryGetBody(binding.body);
				if (body == nullptr) {
					continue;
				}
			}
		}

		body->enabled = binding.node->enabled();

		const uint64_t model = binding.node->getModel().uid().data();
		const uint64_t palette = binding.node->paletteUid();
		if (binding.source_revision != binding.node->revision() || binding.source_model != model ||
		    binding.source_palette != palette) {
			destroyShape(binding.shape);
			binding.shape = createVoxelShape(binding.body, *binding.node);
			binding.node->assignShape(binding.shape);
			binding.source_revision = binding.node->revision();
			binding.source_model = model;
			binding.source_palette = palette;
			if (body->type == BodyType::dynamic_body) {
				rebuildMassProperties(binding.body);
			}
		}

		Shape* shape = tryGetShape(binding.shape);
		if (shape == nullptr) {
			continue;
		}
		if (shape->enabled != body->enabled) {
			shape->enabled = body->enabled;
			incrementShapeRevision(binding.shape);
		}
	}

	for (toast::Box<toast::VoxelNode>& node : voxel_nodes_to_reregister) {
		if (not node.exists()) {
			continue;
		}
		unregisterVoxelNode(*node);
		registerVoxelNode(*node);
	}
}

void Simulator::wakeBody(BodyID id) {
	if (not instance) {
		return;
	}
	if (Body* body = instance->tryGetBody(id); body && body->type == BodyType::dynamic_body) {
		instance->m_profile.bodies_woken += not body->awake;
		body->awake = true;
		body->sleep_timer = 0.0f;
	}
}

void Simulator::sleepBody(BodyID id) {
	if (not instance) {
		return;
	}
	if (Body* body = instance->tryGetBody(id); body && body->type == BodyType::dynamic_body) {
		instance->m_profile.bodies_slept += body->awake;
		body->awake = false;
		body->sleep_timer = sleep_delay;
		body->linear_velocity = {};
		body->angular_velocity = {};
		body->previous_position = body->position;
		body->previous_rotation = body->rotation;
	}
}

void Simulator::wakeBodiesTouching(BodyID id) {
	for (const CachedManifold& manifold : m_cached_manifolds) {
		if (manifold.pair.a.body == id) {
			wakeBody(manifold.pair.b.body);
		} else if (manifold.pair.b.body == id) {
			wakeBody(manifold.pair.a.body);
		}
	}
}

void Simulator::wakeContactGroups() {
	ZoneScopedN("physics::WakeContactGroups");

	bool woke_body = true;
	while (woke_body) {
		woke_body = false;
		for (const Manifold& manifold : m_manifolds) {
			Body* body_a = tryGetBody(manifold.pair.a.body);
			Body* body_b = tryGetBody(manifold.pair.b.body);
			if (not body_a || not body_b) {
				continue;
			}

			const bool a_can_wake = body_a->type == BodyType::dynamic_body && body_a->awake;
			const bool b_can_wake = body_b->type == BodyType::dynamic_body && body_b->awake;
			if (a_can_wake && body_b->type == BodyType::dynamic_body && not body_b->awake) {
				wakeBody(manifold.pair.b.body);
				woke_body = true;
			}
			if (b_can_wake && body_a->type == BodyType::dynamic_body && not body_a->awake) {
				wakeBody(manifold.pair.a.body);
				woke_body = true;
			}
		}
	}
}

void Simulator::setBodyEnabled(BodyID body, bool enabled) {
	ZoneScopedN("physics::SetBodyEnabled");
	ZoneValue(static_cast<uint64_t>(body.slot));

	if (not instance) {
		return;
	}
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}
	if (Body* value = instance->tryGetBody(body)) {
		value->enabled = enabled;
		if (enabled) {
			wakeBody(body);
		}
	}
}

void Simulator::setShapeEnabled(ShapeID shape, bool enabled) {
	ZoneScopedN("physics::SetShapeEnabled");
	ZoneValue(static_cast<uint64_t>(shape.slot));

	if (not instance) {
		return;
	}
	if (not instance->mainThreadMutationAllowed()) {
		return;
	}
	if (Shape* value = instance->tryGetShape(shape)) {
		if (value->enabled != enabled) {
			if (enabled) {
				wakeBody(value->owner);
			}
			value->enabled = enabled;
			instance->incrementShapeRevision(shape);
		}
	}
}

void Simulator::publishTransforms() {
	ZoneScopedN("physics::PublishTransforms");
	ZoneValue(static_cast<uint64_t>(m_node_bindings.size()));

	for (auto binding = m_node_bindings.begin(); binding != m_node_bindings.end();) {
		if (not publishTransform(*binding)) {
			binding = m_node_bindings.erase(binding);
			continue;
		}
		++binding;
	}

	for (auto binding = m_voxel_bindings.begin(); binding != m_voxel_bindings.end();) {
		if (not publishVoxelTransform(*binding)) {
			binding = m_voxel_bindings.erase(binding);
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

	if (body->enabled && body->type == BodyType::dynamic_body) {
		binding.node->applyPhysicsTransform(body->position, body->rotation);
	}
	if (auto dynamic_node = binding.node.as<DynamicRigidbody>(); dynamic_node.exists()) {
		dynamic_node->publishPhysicsState(body->awake, body->linear_velocity, body->angular_velocity);
	}
	return true;
}

auto Simulator::publishVoxelTransform(VoxelNodeBinding& binding) -> bool {
	ZoneScoped;
	ZoneValue(static_cast<uint64_t>(binding.body.slot));

	Body* body = tryGetBody(binding.body);
	if (not binding.node.exists() || not body) {
		return false;
	}

	if (body->type == BodyType::dynamic_body) {
		if (body->enabled) {
			binding.node->applyPhysicsTransform(body->position, body->rotation);
		}
		binding.node->publishPhysicsState(body->awake, body->linear_velocity, body->angular_velocity);
	}
	return true;
}

void Simulator::publishVoxelRenderRecords() {
	ZoneScopedN("physics::PublishVoxelRenderRecords");
	m_voxel_render_records.clear();

	for (const auto& [index, slot] : m_shapes | std::views::enumerate) {
		if (not slot.occupied || slot.shape.type != ShapeType::voxel) {
			continue;
		}

		const VoxelShapeData* data = tryGetVoxelData(slot.shape.voxel.data);
		const Body* body = tryGetBody(slot.shape.owner);
		if (data == nullptr || body == nullptr) {
			continue;
		}

		const glm::mat4 body_transform = glm::translate(glm::mat4(1.0f), body->position) * glm::mat4_cast(body->rotation);
		const glm::mat4 local_transform =
		    glm::translate(glm::mat4(1.0f), slot.shape.voxel.local_center) * glm::mat4_cast(slot.shape.voxel.local_rotation);

		m_voxel_render_records.push_back(
		    VoxelRenderRecord {
		      .shape = ShapeID {.slot = static_cast<uint32_t>(index), .generation = slot.generation},
		      .volume = &data->volume,
		      .palette = &data->palette,
		      .transform = body_transform * local_transform,
		      .revision = data->surface_revision,
		}
		);
	}

	ZoneValue(static_cast<uint64_t>(m_voxel_render_records.size()));
}

auto Simulator::velocityAtPoint(const Body& body, const glm::vec3& r) -> glm::vec3 {
	return body.linear_velocity + glm::cross(body.angular_velocity, r);
}

auto Simulator::effectiveMassAlong(
    const Body& body_a, const Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction
) -> std::optional<float> {
	ZoneScopedN("physics::EffectiveMass");

	glm::vec3 angular_a = body_a.inverse_inertia_world * glm::cross(r_a, direction);
	glm::vec3 angular_b = body_b.inverse_inertia_world * glm::cross(r_b, direction);
	float denominator =
	    body_a.inverse_mass + body_b.inverse_mass + glm::dot(direction, glm::cross(angular_a, r_a) + glm::cross(angular_b, r_b));

	if (not std::isfinite(denominator) || denominator <= 1.0e-8f) {
		return std::nullopt;
	}

	return 1.0f / denominator;
}

void Simulator::applyImpulse(Body& body_a, Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse) {
	ZoneScopedN("physics::ApplyImpulse");

	if (body_a.inverse_mass > 0.0f) {
		body_a.linear_velocity -= impulse * body_a.inverse_mass;
		body_a.angular_velocity -= body_a.inverse_inertia_world * glm::cross(r_a, impulse);
	}
	if (body_b.inverse_mass > 0.0f) {
		body_b.linear_velocity += impulse * body_b.inverse_mass;
		body_b.angular_velocity += body_b.inverse_inertia_world * glm::cross(r_b, impulse);
	}
}

auto Simulator::solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool {
	ZoneScopedN("physics::SolveNormal");

	glm::vec3 relative_velocity = velocityAtPoint(body_b, constraint.r_b) - velocityAtPoint(body_a, constraint.r_a);
	float normal_speed = glm::dot(relative_velocity, constraint.normal);
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
	ZoneScopedN("physics::SolveFriction");

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

auto Simulator::correctPositions(std::span<const size_t> manifold_indices) -> size_t {
	ZoneScopedN("physics::CorrectPositions");
	ZoneValue(static_cast<uint64_t>(manifold_indices.size()));
	size_t correction_count = 0;

	constexpr float penetration_slop = 0.005f;
	constexpr float correction_beta = 0.2f;
	constexpr float max_correction = 0.05f;

	for (const size_t manifold_index : manifold_indices) {
		const Manifold& manifold = m_manifolds[manifold_index];
		if (not shouldSolve(manifold)) {
			continue;
		}

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

		if (body_a->inverse_mass > 0.0f) {
			body_a->position -= correction * body_a->inverse_mass;
		}
		if (body_b->inverse_mass > 0.0f) {
			body_b->position += correction * body_b->inverse_mass;
		}
		++correction_count;
	}

	return correction_count;
}

auto Simulator::shouldSolve(const Manifold& manifold) const -> bool {
	const Body* body_a = tryGetBody(manifold.pair.a.body);
	const Body* body_b = tryGetBody(manifold.pair.b.body);
	if (not body_a || not body_b) {
		return false;
	}

	const bool a_is_active = body_a->type == BodyType::dynamic_body && body_a->enabled && body_a->awake;
	const bool b_is_active = body_b->type == BodyType::dynamic_body && body_b->enabled && body_b->awake;
	return a_is_active || b_is_active;
}

auto Simulator::generateManifoldsAsync(CollisionWorldView world, std::span<const BroadPhasePair> candidates)
    -> std::vector<Manifold> {
	ZoneScopedN("physics::NarrowPhase");

	constexpr size_t minimum_candidates_per_job = 4;
	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), 1ull);
	const size_t maximum_job_count = worker_count * 3;
	const size_t job_count =
	    candidates.empty() ? 0 : std::min(maximum_job_count, std::max(candidates.size() / minimum_candidates_per_job, size_t {1}));

	std::vector<std::future<ManifoldQueue>> futures;
	futures.reserve(job_count);

	for (size_t job_index = 0; job_index < job_count; ++job_index) {
		const size_t begin = job_index * candidates.size() / job_count;
		const size_t end = (job_index + 1) * candidates.size() / job_count;
		auto batch = candidates.subspan(begin, end - begin);

		futures.emplace_back(toast::ThreadPool::push([this, world, batch] {
			ZoneScopedN("physics::NarrowPhaseBatch");
			ZoneValue(static_cast<uint64_t>(batch.size()));
			return m_narrow_phase.generateManifolds(world, batch);
		}));
	}
	m_profile.narrow_jobs = futures.size();
	m_profile.narrow_candidates = candidates.size();

	std::vector<Manifold> merged;
	merged.reserve(candidates.size());

	{
		ZoneScopedNC("physics::NarrowPhaseAwait", 0x202020);
		for (auto& future : futures) {
			ManifoldQueue queue = future.get();
			m_profile.narrow_collisions += queue.collision_count;
			m_profile.rejected_manifolds += queue.rejected_manifold_count;
			m_profile.contact_points += queue.contact_count;
			for (size_t type = 0; type < queue.pair_candidates.size(); ++type) {
				m_profile.narrow_pair_candidates[type] += queue.pair_candidates[type];
			}
			merged.insert_range(merged.end(), std::move(queue.manifolds));
		}
	}

	{
		ZoneScopedN("physics::SortManifolds");
		ZoneValue(static_cast<uint64_t>(merged.size()));
		std::ranges::sort(merged, [](const Manifold& lhs, const Manifold& rhs) { return lhs < rhs; });
	}

	return merged;
}

void Simulator::updateSleeping(float dt) {
	ZoneScopedN("physics::UpdateSleeping");
	if (not std::isfinite(dt) || dt <= 0.0f) {
		return;
	}

	std::vector<size_t> parents(m_bodies.size());
	for (size_t index = 0; index < parents.size(); ++index) {
		parents[index] = index;
	}

	auto find_root = [&parents](size_t index) {
		while (parents[index] != index) {
			parents[index] = parents[parents[index]];
			index = parents[index];
		}
		return index;
	};

	for (const Manifold& manifold : m_manifolds) {
		Body* body_a = tryGetBody(manifold.pair.a.body);
		Body* body_b = tryGetBody(manifold.pair.b.body);
		if (not body_a || not body_b || body_a->type != BodyType::dynamic_body || body_b->type != BodyType::dynamic_body) {
			continue;
		}

		const size_t root_a = find_root(manifold.pair.a.body.slot);
		const size_t root_b = find_root(manifold.pair.b.body.slot);
		if (root_a != root_b) {
			parents[root_b] = root_a;
		}
	}

	for (size_t index = 0; index < m_bodies.size(); ++index) {
		BodySlot& slot = m_bodies[index];
		Body& body = slot.body;
		if (not slot.occupied || not body.enabled || body.type != BodyType::dynamic_body) {
			continue;
		}

		if (not body.allow_sleep) {
			wakeBody(BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation});
			continue;
		}

		const float linear_speed_squared = glm::dot(body.linear_velocity, body.linear_velocity);
		const float angular_speed_squared = glm::dot(body.angular_velocity, body.angular_velocity);
		const bool is_still = std::isfinite(linear_speed_squared) && std::isfinite(angular_speed_squared) &&
		                      linear_speed_squared <= sleep_linear_threshold_squared &&
		                      angular_speed_squared <= sleep_angular_threshold_squared;
		if (is_still) {
			body.sleep_timer = std::min(body.sleep_timer + dt, sleep_delay);
		} else {
			wakeBody(BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation});
		}
	}

	std::vector<bool> group_exists(m_bodies.size(), false);
	std::vector<bool> group_can_sleep(m_bodies.size(), true);
	for (size_t index = 0; index < m_bodies.size(); ++index) {
		const BodySlot& slot = m_bodies[index];
		const Body& body = slot.body;
		if (not slot.occupied || not body.enabled || body.type != BodyType::dynamic_body) {
			continue;
		}

		const size_t root = find_root(index);
		group_exists[root] = true;
		group_can_sleep[root] = group_can_sleep[root] && body.allow_sleep && body.sleep_timer >= sleep_delay;
	}

	for (size_t index = 0; index < m_bodies.size(); ++index) {
		const BodySlot& slot = m_bodies[index];
		const Body& body = slot.body;
		if (not slot.occupied || not body.enabled || body.type != BodyType::dynamic_body) {
			continue;
		}

		const size_t root = find_root(index);
		if (group_exists[root] && group_can_sleep[root]) {
			sleepBody(BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation});
		}
	}
}

void Simulator::updateCache(std::span<const Manifold> manifolds) {
	ZoneScopedN("physics::UpdateContactCache");
	ZoneValue(static_cast<uint64_t>(manifolds.size()));

	std::vector<CachedManifold> next_cache;
	next_cache.reserve(manifolds.size());

	for (const Manifold& manifold : manifolds) {
		const uint32_t revision_a = shapeRevision(manifold.pair.a.shape);
		const uint32_t revision_b = shapeRevision(manifold.pair.b.shape);
		const auto old_manifold = std::ranges::find_if(m_cached_manifolds, [&manifold](const CachedManifold& cached) {
			return cached.pair == manifold.pair && cached.normal_index == manifold.normal_index;
		});
		const bool revisions_match = old_manifold != m_cached_manifolds.end() && old_manifold->shape_a_revision == revision_a &&
		                             old_manifold->shape_b_revision == revision_b;

		if (revisions_match) {
			++m_profile.contact_persists;
			event::send<event::ContactPersist>(manifold);
		} else {
			wakeBody(manifold.pair.a.body);
			wakeBody(manifold.pair.b.body);
			if (old_manifold != m_cached_manifolds.end()) {
				++m_profile.contact_ends;
				event::send<event::ContactEnd>(old_manifold->pair);
			}
			++m_profile.contact_begins;
			event::send<event::ContactBegin>(manifold);
		}

		CachedManifold next_manifold {
		  .pair = manifold.pair,
		  .normal_index = manifold.normal_index,
		  .shape_a_revision = revision_a,
		  .shape_b_revision = revision_b,
		  .contact_count = static_cast<uint8_t>(std::min<size_t>(manifold.contact_count, manifold.contacts.size())),
		};

		for (size_t contact_index = 0; contact_index < next_manifold.contact_count; ++contact_index) {
			const ContactPoint& current_contact = manifold.contacts[contact_index];
			CachedContact& next_contact = next_manifold.contacts[contact_index];
			next_contact.feature_a = current_contact.feature_a;
			next_contact.feature_b = current_contact.feature_b;

			if (not revisions_match) {
				++m_profile.cold_cached_contacts;
				continue;
			}

			const auto old_contact_end = old_manifold->contacts.begin() + old_manifold->contact_count;
			const auto old_contact =
			    std::find_if(old_manifold->contacts.begin(), old_contact_end, [&current_contact](const CachedContact& cached) {
				    return cached.feature_a == current_contact.feature_a && cached.feature_b == current_contact.feature_b;
			    });
			if (old_contact != old_contact_end) {
				++m_profile.reused_cached_contacts;
				next_contact.normal_impulse = old_contact->normal_impulse;
				next_contact.tangent_impulse = old_contact->tangent_impulse;
			} else {
				++m_profile.cold_cached_contacts;
			}
		}

		next_cache.emplace_back(next_manifold);
	}

	for (const CachedManifold& cached : m_cached_manifolds) {
		const bool still_colliding = std::ranges::any_of(manifolds, [&cached](const Manifold& manifold) {
			return manifold.pair == cached.pair && manifold.normal_index == cached.normal_index;
		});
		if (not still_colliding) {
			wakeBody(cached.pair.a.body);
			wakeBody(cached.pair.b.body);
			++m_profile.contact_ends;
			event::send<event::ContactEnd>(cached.pair);
		}
	}

	m_cached_manifolds = std::move(next_cache);
	ZoneValue(static_cast<uint64_t>(m_cached_manifolds.size()));
}

void Simulator::integrate(float dt) {
	ZoneScopedN("physics::IntegrateBodies");
	ZoneValue(static_cast<uint64_t>(m_bodies.size()));
	if (not mainThreadMutationAllowed()) {
		return;
	}

	if (not std::isfinite(dt) or dt <= 0.0f) {
		return;
	}

	for (size_t index = 0; index < m_bodies.size(); ++index) {
		BodySlot& slot = m_bodies[index];
		if (not slot.occupied) {
			continue;
		}

		integrateBody(BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation}, slot.body, gravity, dt);
	}
}

void Simulator::integrateBody(BodyID id, Body& body, const glm::vec3& gravity, float dt) {
	ZoneScopedN("physics::IntegrateBody");
	ZoneValue(static_cast<uint64_t>(id.slot));
	if (not body.enabled) {
		return;
	}

	if (body.type == BodyType::dynamic_body && body.awake == false) {
		body.linear_velocity = glm::vec3 {0.0f};
		body.angular_velocity = glm::vec3 {0.0f};
		return;
	}

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
	glm::quat omega_q = {0.0f, body.angular_velocity.x, body.angular_velocity.y, body.angular_velocity.z};
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
	ZoneScopedN("physics::CreateBody");
	if (not mainThreadMutationAllowed()) {
		return {};
	}

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
	  .allow_sleep = descriptor.type == BodyType::dynamic_body && descriptor.allow_sleep,
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
	ZoneScopedN("physics::DestroyBody");
	ZoneValue(static_cast<uint64_t>(body.slot));
	if (not mainThreadMutationAllowed()) {
		return;
	}

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
	ZoneScopedN("physics::CreateSphere");
	ZoneValue(static_cast<uint64_t>(owner.slot));
	if (not mainThreadMutationAllowed()) {
		return {};
	}

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
	ZoneScopedN("physics::CreateBox");
	ZoneValue(static_cast<uint64_t>(owner.slot));
	if (not mainThreadMutationAllowed()) {
		return {};
	}

	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected box registration for an invalid body");
		return {};
	}

	const bool center_is_finite =
	    std::isfinite(box.local_center.x) && std::isfinite(box.local_center.y) && std::isfinite(box.local_center.z);
	const bool size_is_valid = std::isfinite(box.size.x) && box.size.x > 0.0f && std::isfinite(box.size.y) && box.size.y > 0.0f &&
	                           std::isfinite(box.size.z) && box.size.z > 0.0f;
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
	ZoneScopedN("physics::CreateCapsule");
	ZoneValue(static_cast<uint64_t>(owner.slot));
	if (not mainThreadMutationAllowed()) {
		return {};
	}

	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected capsule registration for an invalid body");
		return {};
	}

	const bool center_is_finite =
	    std::isfinite(capsule.local_center.x) && std::isfinite(capsule.local_center.y) && std::isfinite(capsule.local_center.z);
	const bool dimensions_are_valid = std::isfinite(capsule.radius) && capsule.radius > 0.0f && std::isfinite(capsule.height) &&
	                                  capsule.height >= 2.0f * capsule.radius;
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

auto Simulator::createVoxelShape(
    BodyID owner, const VoxelShape& shape, const assets::VoxelModel& model, const voxel::Palette& palette,
    const voxel::MaterialLibrary& materials
) -> ShapeID {
	ZoneScopedN("physics::CreateVoxelShape");
	ZoneValue(static_cast<uint64_t>(owner.slot));
	if (not mainThreadMutationAllowed()) {
		return {};
	}

	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected voxel shape registration for an invalid body");
		return {};
	}

	const bool center_is_finite =
	    std::isfinite(shape.local_center.x) && std::isfinite(shape.local_center.y) && std::isfinite(shape.local_center.z);
	const float rotation_length_squared = glm::dot(shape.local_rotation, shape.local_rotation);
	const glm::uvec3 brick_dims = model.brickDims();
	if (not center_is_finite || not std::isfinite(rotation_length_squared) || rotation_length_squared <= 1.0e-10f ||
	    brick_dims.x == 0 || brick_dims.y == 0 || brick_dims.z == 0) {
		TOAST_WARN("Physics", "Rejected voxel shape with invalid dimensions, local center, or local rotation");
		return {};
	}

	if (not voxel::validateLibrary(materials).empty() || not voxel::validatePalette(palette, materials).empty()) {
		TOAST_WARN("Physics", "Rejected voxel shape with an invalid palette or material library");
		return {};
	}

	std::optional<voxel::Volume> volume = model.instantiate(m_voxel_pool);
	if (not volume.has_value()) {
		TOAST_WARN("Physics", "Rejected voxel shape because the physics brick pool is out of its {} bricks", m_voxel_pool.capacity());
		return {};
	}

	voxel::VolumeSurface surface;
	surface.rebuild(*volume);

	VoxelShapeData voxel_data {
	  .volume = std::move(*volume),
	  .surface = std::move(surface),
	  .palette = palette,
	  .materials = materials,
	};

	VoxelDataID data_id;
	if (not m_free_voxel_shape_slots.empty()) {
		data_id.slot = m_free_voxel_shape_slots.back();
		m_free_voxel_shape_slots.pop_back();
		VoxelShapeSlot& slot = m_voxel_shapes[data_id.slot];
		slot.data.emplace(std::move(voxel_data));
		data_id.generation = slot.generation;
	} else {
		data_id.slot = static_cast<uint32_t>(m_voxel_shapes.size());
		m_voxel_shapes.emplace_back();
		VoxelShapeSlot& slot = m_voxel_shapes.back();
		slot.data.emplace(std::move(voxel_data));
		data_id.generation = slot.generation;
	}

	VoxelShape stored_shape = shape;
	stored_shape.data = data_id;
	stored_shape.local_rotation = glm::normalize(shape.local_rotation);
	stored_shape.local_bounds = {
	  .min = glm::vec3(0.0f),
	  .max = glm::vec3(brick_dims) * voxel::k_brick_size,
	};

	Shape physics_shape {
	  .owner = owner,
	  .type = ShapeType::voxel,
	  .voxel = stored_shape,
	};

	if (not m_free_shape_slots.empty()) {
		const uint32_t index = m_free_shape_slots.back();
		m_free_shape_slots.pop_back();
		ShapeSlot& slot = m_shapes[index];
		slot.shape = physics_shape;
		slot.occupied = true;
		return {.slot = index, .generation = slot.generation};
	}

	const uint32_t index = static_cast<uint32_t>(m_shapes.size());
	m_shapes.emplace_back(ShapeSlot {.shape = physics_shape, .generation = 1, .occupied = true});
	return {.slot = index, .generation = 1};
}

auto Simulator::createVoxelShape(BodyID owner, toast::VoxelNode& node) -> ShapeID {
	const assets::VoxelModel* model = node.resolvedModel();
	const voxel::Palette* palette = node.resolvedPalette();
	const voxel::MaterialLibrary* materials = node.resolvedMaterialLibrary();
	if (model == nullptr || palette == nullptr || materials == nullptr) {
		TOAST_WARN("Physics", "Voxel node '{}' is missing a valid model, palette, or physical material library", node.name());
		return {};
	}

	node.syncTransform();
	const bool has_unit_scale = glm::all(glm::lessThanEqual(glm::abs(node.world_scale - glm::vec3(1.0f)), glm::vec3(1.0e-5f)));
	if (not has_unit_scale) {
		TOAST_WARN("Physics", "Voxel node '{}' cannot register with non-unit world scale", node.name());
		return {};
	}

	const VoxelShape voxel_shape;

	const ShapeID shape = createVoxelShape(owner, voxel_shape, *model, *palette, *materials);
	const Shape* stored_shape = tryGetShape(shape);
	if (stored_shape != nullptr) {
		if (VoxelShapeData* data = tryGetVoxelData(stored_shape->voxel.data)) {
			data->source_revision = node.revision();
		}
	}
	return shape;
}

void Simulator::destroyShape(ShapeID shape) {
	ZoneScopedN("physics::DestroyShape");
	ZoneValue(static_cast<uint64_t>(shape.slot));
	if (not mainThreadMutationAllowed()) {
		return;
	}

	if (not valid(shape)) {
		return;
	}

	ShapeSlot& slot = m_shapes[shape.slot];
	if (slot.shape.type == ShapeType::voxel) {
		destroyVoxelData(slot.shape.voxel.data);
	}
	slot.occupied = false;
	++slot.revision;
	if (slot.revision == 0) {
		++slot.revision;
	}
	++slot.generation;
	if (slot.generation == 0) {
		++slot.generation;
	}
	m_free_shape_slots.emplace_back(shape.slot);
}

auto Simulator::valid(ShapeID shape) const -> bool {
	return shape.slot < m_shapes.size() && m_shapes[shape.slot].occupied && m_shapes[shape.slot].generation == shape.generation;
}

auto Simulator::shapeRevision(ShapeID shape) const -> uint32_t {
	return valid(shape) ? m_shapes[shape.slot].revision : 0;
}

void Simulator::incrementShapeRevision(ShapeID shape) {
	if (not valid(shape)) {
		return;
	}

	ShapeSlot& slot = m_shapes[shape.slot];
	++slot.revision;
	if (slot.revision == 0) {
		++slot.revision;
	}
}

auto Simulator::tryGetShape(ShapeID shape) -> Shape* {
	return valid(shape) ? &m_shapes[shape.slot].shape : nullptr;
}

auto Simulator::tryGetShape(ShapeID shape) const -> const Shape* {
	return valid(shape) ? &m_shapes[shape.slot].shape : nullptr;
}

auto Simulator::valid(VoxelDataID data) const -> bool {
	return data.slot < m_voxel_shapes.size() && m_voxel_shapes[data.slot].data.has_value() &&
	       m_voxel_shapes[data.slot].generation == data.generation;
}

auto Simulator::tryGetVoxelData(VoxelDataID data) -> VoxelShapeData* {
	if (not mainThreadMutationAllowed()) {
		return nullptr;
	}
	return valid(data) ? &*m_voxel_shapes[data.slot].data : nullptr;
}

auto Simulator::tryGetVoxelData(VoxelDataID data) const -> const VoxelShapeData* {
	if (not mainThreadMutationAllowed()) {
		return nullptr;
	}
	return valid(data) ? &*m_voxel_shapes[data.slot].data : nullptr;
}

void Simulator::destroyVoxelData(VoxelDataID data) {
	if (not mainThreadMutationAllowed()) {
		return;
	}
	if (not valid(data)) {
		return;
	}

	VoxelShapeSlot& slot = m_voxel_shapes[data.slot];
	slot.data.reset();
	++slot.generation;
	if (slot.generation == 0) {
		++slot.generation;
	}
	m_free_voxel_shape_slots.emplace_back(data.slot);
}

void Simulator::rebuildMassProperties(BodyID id) {
	ZoneScopedN("physics::RebuildMassProperties");
	ZoneValue(static_cast<uint64_t>(id.slot));

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
		if (not slot.occupied) {
			continue;
		}
		if (slot.shape.owner != id) {
			continue;
		}
		shapes.emplace_back(
		    ShapeID {
		      .slot = static_cast<uint32_t>(index),
		      .generation = slot.generation,
		    }
		);
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
		case ShapeType::voxel: {
			const glm::vec3 size = shape->voxel.local_bounds.max - shape->voxel.local_bounds.min;
			const glm::vec3 size_squared = size * size;
			const glm::vec3 denominator {
			  size_squared.y + size_squared.z,
			  size_squared.x + size_squared.z,
			  size_squared.x + size_squared.y,
			};
			if (not std::isfinite(denominator.x) || not std::isfinite(denominator.y) || not std::isfinite(denominator.z) ||
			    glm::any(glm::lessThanEqual(denominator, glm::vec3(0.0f)))) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid voxel inertia denominator");
				return;
			}

			glm::mat3 inverse_inertia {0.0f};
			inverse_inertia[0][0] = 12.0f * body->inverse_mass / denominator.x;
			inverse_inertia[1][1] = 12.0f * body->inverse_mass / denominator.y;
			inverse_inertia[2][2] = 12.0f * body->inverse_mass / denominator.z;
			const glm::mat3 local_rotation = glm::mat3_cast(shape->voxel.local_rotation);
			body->inverse_inertia_local = local_rotation * inverse_inertia * glm::transpose(local_rotation);
			const glm::mat3 rotation = glm::mat3_cast(body->rotation);
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
	  .awake = value->awake,
	  .position = value->position,
	  .rotation = value->rotation,
	  .previous_position = value->previous_position,
	  .previous_rotation = value->previous_rotation,
	  .linear_velocity = value->linear_velocity,
	  .angular_velocity = value->angular_velocity
	};
}

auto Simulator::setTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation) -> bool {
	ZoneScopedN("physics::SetTransform");
	ZoneValue(static_cast<uint64_t>(body.slot));
	if (not mainThreadMutationAllowed()) {
		return false;
	}

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
	wakeBody(body);
	wakeBodiesTouching(body);
	return true;
}

auto Simulator::setLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool {
	ZoneScopedN("physics::SetLinearVelocity");
	ZoneValue(static_cast<uint64_t>(body.slot));
	if (not mainThreadMutationAllowed()) {
		return false;
	}

	Body* value = tryGetBody(body);
	if (not value) {
		return false;
	}

	value->linear_velocity = velocity;
	wakeBody(body);
	wakeBodiesTouching(body);
	return true;
}

auto Simulator::tryGetBody(BodyID body) -> Body* {
	return valid(body) ? &m_bodies[body.slot].body : nullptr;
}

auto Simulator::tryGetBody(BodyID body) const -> const Body* {
	return valid(body) ? &m_bodies[body.slot].body : nullptr;
}

auto Simulator::findCachedContact(
    const BroadPhasePair& pair, uint8_t normal_index, ContactFeatureID feature_a, ContactFeatureID feature_b
) -> CachedContact* {
	ZoneScopedN("physics::FindCachedContact");

	const auto manifold = std::ranges::find_if(m_cached_manifolds, [&pair, normal_index](const CachedManifold& cached) {
		return cached.pair == pair && cached.normal_index == normal_index;
	});
	if (manifold == m_cached_manifolds.end()) {
		return nullptr;
	}

	const size_t contact_count = std::min<size_t>(manifold->contact_count, manifold->contacts.size());
	const auto contact = std::find_if(
	    manifold->contacts.begin(),
	    manifold->contacts.begin() + contact_count,
	    [feature_a, feature_b](const CachedContact& cached) {
		    return cached.feature_a == feature_a && cached.feature_b == feature_b;
	    }
	);
	return contact != manifold->contacts.begin() + contact_count ? &*contact : nullptr;
}

auto Simulator::findCachedContact(
    const BroadPhasePair& pair, uint8_t normal_index, ContactFeatureID feature_a, ContactFeatureID feature_b
) const -> const CachedContact* {
	ZoneScopedN("physics::FindCachedContact");

	const auto manifold = std::ranges::find_if(m_cached_manifolds, [&pair, normal_index](const CachedManifold& cached) {
		return cached.pair == pair && cached.normal_index == normal_index;
	});
	if (manifold == m_cached_manifolds.end()) {
		return nullptr;
	}

	const size_t contact_count = std::min<size_t>(manifold->contact_count, manifold->contacts.size());
	const auto contact = std::find_if(
	    manifold->contacts.begin(),
	    manifold->contacts.begin() + contact_count,
	    [feature_a, feature_b](const CachedContact& cached) {
		    return cached.feature_a == feature_a && cached.feature_b == feature_b;
	    }
	);
	return contact != manifold->contacts.begin() + contact_count ? &*contact : nullptr;
}

auto Simulator::prepareConstraints(const std::vector<Manifold>& manifolds) -> std::vector<Constraint> {
	ZoneScopedN("physics::PrepareConstraints");
	std::vector<Constraint> constraints;
	size_t contact_count = 0;
	for (const Manifold& manifold : manifolds) {
		contact_count += std::min<size_t>(manifold.contact_count, manifold.contacts.size());
	}
	constraints.reserve(contact_count);

	size_t rejected_contact_count = 0;

	for (const Manifold& manifold : manifolds) {
		if (not shouldSolve(manifold)) {
			continue;
		}

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
	m_profile.constraints = constraints.size();
	m_profile.rejected_constraints = rejected_contact_count;
	m_profile.warm_started_constraints = std::ranges::count_if(constraints, [](const Constraint& constraint) {
		return constraint.accumulated_normal_impulse > 0.0f || constraint.accumulated_tangent_impulse != 0.0f;
	});

	ZoneValue(static_cast<uint64_t>(constraints.size()));
	return constraints;
}

auto Simulator::buildIslands(std::span<const Manifold> manifolds, const std::vector<Constraint>& constraints) const
    -> std::vector<SimulationIsland> {
	ZoneScopedN("physics::BuildIslands");

	std::vector<size_t> parents(m_bodies.size());
	std::vector<bool> participates(m_bodies.size(), false);
	for (size_t index = 0; index < parents.size(); ++index) {
		parents[index] = index;
	}

	auto find_root = [&parents](size_t index) {
		while (parents[index] != index) {
			parents[index] = parents[parents[index]];
			index = parents[index];
		}
		return index;
	};

	const auto is_active_dynamic = [](const Body* body) {
		return body && body->type == BodyType::dynamic_body && body->enabled && body->awake;
	};

	for (const Manifold& manifold : manifolds) {
		if (not shouldSolve(manifold)) {
			continue;
		}

		const Body* body_a = tryGetBody(manifold.pair.a.body);
		const Body* body_b = tryGetBody(manifold.pair.b.body);
		const bool a_is_dynamic = is_active_dynamic(body_a);
		const bool b_is_dynamic = is_active_dynamic(body_b);

		if (a_is_dynamic) {
			participates[manifold.pair.a.body.slot] = true;
		}
		if (b_is_dynamic) {
			participates[manifold.pair.b.body.slot] = true;
		}

		if (a_is_dynamic && b_is_dynamic) {
			const size_t root_a = find_root(manifold.pair.a.body.slot);
			const size_t root_b = find_root(manifold.pair.b.body.slot);
			if (root_a < root_b) {
				parents[root_b] = root_a;
			} else if (root_b < root_a) {
				parents[root_a] = root_b;
			}
		}
	}

	const size_t no_island = m_bodies.size();
	std::vector<size_t> island_by_root(m_bodies.size(), no_island);
	std::vector<SimulationIsland> islands;

	for (size_t body_index = 0; body_index < m_bodies.size(); ++body_index) {
		if (not participates[body_index]) {
			continue;
		}

		const size_t root = find_root(body_index);
		if (island_by_root[root] == no_island) {
			const BodySlot& root_slot = m_bodies[root];
			island_by_root[root] = islands.size();
			islands.emplace_back(
			    SimulationIsland {
			      .sort_key = BodyID {.slot = static_cast<uint32_t>(root), .generation = root_slot.generation},
			}
			);
		}

		const BodySlot& slot = m_bodies[body_index];
		islands[island_by_root[root]].dynamic_bodies.emplace_back(
		    BodyID {.slot = static_cast<uint32_t>(body_index), .generation = slot.generation}
		);
	}

	const auto island_for_pair = [&](const BroadPhasePair& pair) -> size_t {
		const Body* body_a = tryGetBody(pair.a.body);
		if (is_active_dynamic(body_a)) {
			return island_by_root[find_root(pair.a.body.slot)];
		}

		const Body* body_b = tryGetBody(pair.b.body);
		if (is_active_dynamic(body_b)) {
			return island_by_root[find_root(pair.b.body.slot)];
		}

		return no_island;
	};

	for (size_t manifold_index = 0; manifold_index < manifolds.size(); ++manifold_index) {
		if (not shouldSolve(manifolds[manifold_index])) {
			continue;
		}

		const size_t island_index = island_for_pair(manifolds[manifold_index].pair);
		if (island_index != no_island) {
			islands[island_index].manifold_indices.emplace_back(manifold_index);
		}
	}

	for (const Constraint& constraint : constraints) {
		const size_t island_index = island_for_pair(constraint.pair);
		if (island_index != no_island) {
			islands[island_index].constraints.emplace_back(constraint);
		}
	}

	for (SimulationIsland& island : islands) {
		std::ranges::sort(island.constraints, [](const Constraint& lhs, const Constraint& rhs) {
			if (lhs.pair != rhs.pair) {
				return lhs.pair < rhs.pair;
			}
			if (lhs.feature_a != rhs.feature_a) {
				return lhs.feature_a < rhs.feature_a;
			}
			return lhs.feature_b < rhs.feature_b;
		});
	}

	std::ranges::sort(islands, [](const SimulationIsland& lhs, const SimulationIsland& rhs) {
		return lhs.sort_key < rhs.sort_key;
	});

	ZoneValue(static_cast<uint64_t>(islands.size()));
	return islands;
}

auto Simulator::prepareConstraint(const Manifold& manifold, const ContactPoint& contact) const -> std::optional<Constraint> {
	ZoneScopedN("physics::PrepareConstraint");
	ZoneValue((static_cast<uint64_t>(manifold.pair.a.body.slot) << 32) | static_cast<uint64_t>(manifold.pair.b.body.slot));

	const Body* body_a = tryGetBody(manifold.pair.a.body);
	const Body* body_b = tryGetBody(manifold.pair.b.body);
	if (not body_a || not body_b) {
		return std::nullopt;
	}

	const bool normal_is_finite =
	    std::isfinite(manifold.normal.x) && std::isfinite(manifold.normal.y) && std::isfinite(manifold.normal.z);
	const bool contact_is_finite =
	    std::isfinite(contact.position.x) && std::isfinite(contact.position.y) && std::isfinite(contact.position.z);
	if (not normal_is_finite || not contact_is_finite || not std::isfinite(contact.penetration) || contact.penetration < 0.0f) {
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
	const CachedContact* cached_contact =
	    findCachedContact(manifold.pair, manifold.normal_index, contact.feature_a, contact.feature_b);
	const float restitution = contact.material.restitution;
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
		tangent = tangent_velocity / sqrt(tangent_length_sq);
	} else if (cached_contact) {
		const glm::vec3 projected_tangent =
		    cached_contact->tangent_impulse - manifold.normal * glm::dot(cached_contact->tangent_impulse, manifold.normal);
		const float projected_length_sq = glm::dot(projected_tangent, projected_tangent);
		if (projected_length_sq > 1.0e-10f && std::isfinite(projected_length_sq)) {
			tangent = projected_tangent / sqrt(projected_length_sq);
		}
	}

	if (glm::dot(tangent, tangent) > 0.0f) {
		const auto calculated_tangent_mass = effectiveMassAlong(*body_a, *body_b, r_a, r_b, tangent);
		if (calculated_tangent_mass.has_value()) {
			tangent_mass = calculated_tangent_mass.value();
		}
	}

	float accumulated_normal_impulse = 0.0f;
	float accumulated_tangent_impulse = 0.0f;
	if (cached_contact && std::isfinite(cached_contact->normal_impulse)) {
		accumulated_normal_impulse = std::max(cached_contact->normal_impulse, 0.0f);
		const bool tangent_impulse_is_finite = std::isfinite(cached_contact->tangent_impulse.x) &&
		                                       std::isfinite(cached_contact->tangent_impulse.y) &&
		                                       std::isfinite(cached_contact->tangent_impulse.z);
		if (tangent_mass > 0.0f && tangent_impulse_is_finite) {
			accumulated_tangent_impulse = glm::dot(cached_contact->tangent_impulse, tangent);
			const float static_limit = contact.material.static_friction * accumulated_normal_impulse;
			accumulated_tangent_impulse = std::clamp(accumulated_tangent_impulse, -static_limit, static_limit);
		}
	}

	return Constraint {
	  .pair = manifold.pair,
	  .normal_index = manifold.normal_index,
	  .feature_a = contact.feature_a,
	  .feature_b = contact.feature_b,
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
	  .static_friction = contact.material.static_friction,
	  .dynamic_friction = contact.material.dynamic_friction,
	  .accumulated_normal_impulse = accumulated_normal_impulse,
	  .accumulated_tangent_impulse = accumulated_tangent_impulse,
	};
}

void Simulator::warmStartConstraints(std::span<Constraint> constraints) {
	ZoneScopedN("physics::WarmStartConstraints");

	for (Constraint& constraint : constraints) {
		Body* body_a = tryGetBody(constraint.body_a);
		Body* body_b = tryGetBody(constraint.body_b);
		if (not body_a || not body_b) {
			continue;
		}

		const glm::vec3 impulse =
		    constraint.normal * constraint.accumulated_normal_impulse + constraint.tangent * constraint.accumulated_tangent_impulse;
		const bool impulse_is_finite = std::isfinite(impulse.x) && std::isfinite(impulse.y) && std::isfinite(impulse.z);
		if (impulse_is_finite) {
			applyImpulse(*body_a, *body_b, constraint.r_a, constraint.r_b, impulse);
		}
	}
}

void Simulator::storeConstraintImpulses(std::span<const Constraint> constraints) {
	ZoneScopedN("physics::StoreConstraintImpulses");

	for (const Constraint& constraint : constraints) {
		CachedContact* cached =
		    findCachedContact(constraint.pair, constraint.normal_index, constraint.feature_a, constraint.feature_b);
		if (not cached) {
			continue;
		}

		cached->normal_impulse = constraint.accumulated_normal_impulse;
		cached->tangent_impulse = constraint.tangent * constraint.accumulated_tangent_impulse;
	}
}

auto Simulator::solveIsland(SimulationIsland& island) -> IslandSolveStats {
	ZoneScopedN("physics::SolveIsland");
	ZoneValue(static_cast<uint64_t>(island.constraints.size()));

	constexpr uint32_t solver_iterations = 8;    // try 4 or 16
	size_t invalid_constraint_count = 0;
	warmStartConstraints(island.constraints);

	for (uint32_t iteration = 0; iteration < solver_iterations; ++iteration) {
		ZoneScopedN("iteration");
		ZoneValue(static_cast<uint64_t>(iteration));
		for (Constraint& constraint : island.constraints) {
			if (not solveConstraint(constraint)) {
				++invalid_constraint_count;
			}
		}
	}

	return {
	  .invalid_constraints = invalid_constraint_count,
	  .position_corrections = correctPositions(island.manifold_indices),
	};
}

void Simulator::solveIslands(std::vector<SimulationIsland>& islands) {
	ZoneScopedN("physics::SolveIslands");
	ZoneValue(static_cast<uint64_t>(islands.size()));

	if (islands.empty()) {
		return;
	}
	PhaseScope worker_phase {*this, SimulationPhase::mutation, SimulationPhase::worker_execution};

	constexpr size_t minimum_islands_per_job = 1;
	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), 1ull);
	const size_t maximum_job_count = worker_count * 3;
	const size_t job_count = std::min(maximum_job_count, std::max(islands.size() / minimum_islands_per_job, size_t {1}));

	std::vector<std::future<IslandSolveStats>> futures;
	futures.reserve(job_count);

	for (size_t job_index = 0; job_index < job_count; ++job_index) {
		const size_t begin = job_index * islands.size() / job_count;
		const size_t end = (job_index + 1) * islands.size() / job_count;
		auto batch = std::span<SimulationIsland> {islands}.subspan(begin, end - begin);
		size_t constraint_count = 0;
		for (const SimulationIsland& island : batch) {
			constraint_count += island.constraints.size();
		}

		futures.emplace_back(toast::ThreadPool::push([this, batch, constraint_count] {
			ZoneScopedN("physics::IslandBatch");
			ZoneValue(static_cast<uint64_t>(constraint_count));

			IslandSolveStats stats;
			for (SimulationIsland& island : batch) {
				const IslandSolveStats island_stats = solveIsland(island);
				stats.invalid_constraints += island_stats.invalid_constraints;
				stats.position_corrections += island_stats.position_corrections;
			}
			return stats;
		}));
	}
	m_profile.island_jobs = futures.size();

	size_t invalid_constraint_count = 0;
	size_t position_correction_count = 0;
	{
		ZoneScopedNC("physics::IslandSolveAwait", 0x202020);
		for (auto& future : futures) {
			const IslandSolveStats stats = future.get();
			invalid_constraint_count += stats.invalid_constraints;
			position_correction_count += stats.position_corrections;
		}
	}

	if (invalid_constraint_count > 0) {
		TOAST_WARN("Physics", "Skipped {} invalid solver constraint(s)", invalid_constraint_count);
	}
	m_profile.invalid_constraints = invalid_constraint_count;
	m_profile.position_corrections = position_correction_count;

	for (const SimulationIsland& island : islands) {
		storeConstraintImpulses(island.constraints);
	}
}

auto Simulator::solveConstraint(Constraint& constraint) -> bool {
	ZoneScopedN("physics::SolveConstraint");

	Body* body_a = tryGetBody(constraint.body_a);
	Body* body_b = tryGetBody(constraint.body_b);

	if (!body_a || !body_b) {
		return false;
	}

	if (!solveNormal(constraint, *body_a, *body_b)) {
		return false;
	}

	return solveFriction(constraint, *body_a, *body_b);
}

}
