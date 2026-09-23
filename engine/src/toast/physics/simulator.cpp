#include "simulator.hpp"

#include "accumulator.hpp"
#include "anchor_mask.hpp"
#include "contact_events.hpp"
#include "fragment_extraction.hpp"
#include "nodes/box_collider.hpp"
#include "nodes/capsule_collider.hpp"
#include "nodes/collider.hpp"
#include "nodes/dynamic_rigidbody.hpp"
#include "nodes/rigidbody.hpp"
#include "nodes/sphere_collider.hpp"
#include "physics_settings.hpp"
#include "voxel_data_lock.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <span>
#include <toast/assets/voxel_model.hpp>
#include <toast/thread_pool.hpp>
#include <toast/voxel/mass_accumulator.hpp>
#include <toast/world/voxel_node.hpp>
#include <tracy/Tracy.hpp>

namespace physics {

namespace {
constexpr float unit_scale_tolerance = 1.0e-4f;

struct CachedManifoldKey {
	const BroadPhasePair& pair;
	uint8_t normal_index;
};

[[nodiscard]]
auto cachedManifoldLess(const CachedManifold& manifold, CachedManifoldKey key) -> bool {
	return manifold.pair < key.pair || (manifold.pair == key.pair && manifold.normal_index < key.normal_index);
}

[[nodiscard]]
auto cachedManifoldLess(const CachedManifold& lhs, const CachedManifold& rhs) -> bool {
	return cachedManifoldLess(lhs, CachedManifoldKey {.pair = rhs.pair, .normal_index = rhs.normal_index});
}

[[nodiscard]]
auto cachedManifoldMatches(const CachedManifold& manifold, CachedManifoldKey key) -> bool {
	return manifold.pair == key.pair && manifold.normal_index == key.normal_index;
}

/// One thread pool job share of a constraint wave may hold pieces of more than one island batch
struct ConstraintChunk {
	std::vector<std::span<Constraint>> pieces;
};

/// Splits a wave per island spans into at most job_count roughly equal chunks cutting across islands freely
auto chunkConstraintWave(std::span<const std::span<Constraint>> spans, size_t job_count) -> std::vector<ConstraintChunk> {
	size_t total = 0;
	for (const std::span<Constraint>& span : spans) {
		total += span.size();
	}
	if (total == 0 || job_count == 0) {
		return {};
	}
	job_count = std::min(job_count, total);
	const size_t target = (total + job_count - 1) / job_count;

	std::vector<ConstraintChunk> chunks;
	chunks.reserve(job_count);
	ConstraintChunk current;
	size_t current_size = 0;
	for (const std::span<Constraint>& span : spans) {
		size_t offset = 0;
		while (offset < span.size()) {
			const size_t room = target - current_size;
			const size_t take = std::min(room, span.size() - offset);
			if (take == 0) {
				chunks.push_back(std::move(current));
				current = ConstraintChunk {};
				current_size = 0;
				continue;
			}
			current.pieces.push_back(span.subspan(offset, take));
			current_size += take;
			offset += take;
		}
	}
	if (!current.pieces.empty()) {
		chunks.push_back(std::move(current));
	}
	return chunks;
}

// TODO: do this but with materials
[[nodiscard]]
auto placeholderMaterialLibrary() -> const voxel::MaterialLibrary& {
	static const voxel::MaterialLibrary library = [] {
		voxel::MaterialLibrary lib;
		lib.materials.assign(voxel::k_max_physical_materials, voxel::PhysicalMaterial {});
		return lib;
	}();
	return library;
}

[[nodiscard]]
auto computeOccupiedBounds(const voxel::Volume& volume) -> AABB {
	ZoneScopedN("physics::ComputeOccupiedBounds");

	const glm::ivec3 brick_dims = glm::ivec3(volume.brickDims());
	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);
	glm::ivec3 min {std::numeric_limits<int32_t>::max()};
	glm::ivec3 max {std::numeric_limits<int32_t>::min()};

	for (int32_t z = 0; z < brick_dims.z; ++z) {
		for (int32_t y = 0; y < brick_dims.y; ++y) {
			for (int32_t x = 0; x < brick_dims.x; ++x) {
				const glm::ivec3 brick {x, y, z};
				if (volume.entryAt(brick).tag() == voxel::BrickTag::empty) {
					continue;
				}

				const glm::ivec3 brick_base = brick * brick_dim;
				for (int32_t lz = 0; lz < brick_dim; ++lz) {
					for (int32_t ly = 0; ly < brick_dim; ++ly) {
						for (int32_t lx = 0; lx < brick_dim; ++lx) {
							const glm::ivec3 voxel_pos = brick_base + glm::ivec3(lx, ly, lz);
							if (not volume.isSolidAt(voxel_pos)) {
								continue;
							}
							min = glm::min(min, voxel_pos);
							max = glm::max(max, voxel_pos);
						}
					}
				}
			}
		}
	}

	if (glm::any(glm::greaterThan(min, max))) {
		// nothing left
		return {};
	}

	return {
	  .min = glm::vec3(min) * voxel::k_voxel_size,
	  .max = glm::vec3(max + glm::ivec3(1)) * voxel::k_voxel_size,
	};
}

[[nodiscard]]
auto accumulateMassMoments(const voxel::Volume& volume, const voxel::Palette& palette, const voxel::MaterialLibrary& materials)
    -> voxel::MassMoments {
	ZoneScopedN("physics::AccumulateMassMoments");

	voxel::MassMoments moments;
	const glm::ivec3 brick_dims = glm::ivec3(volume.brickDims());
	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);
	for (int32_t bz = 0; bz < brick_dims.z; ++bz) {
		for (int32_t by = 0; by < brick_dims.y; ++by) {
			for (int32_t bx = 0; bx < brick_dims.x; ++bx) {
				const glm::ivec3 brick {bx, by, bz};
				if (volume.entryAt(brick).tag() == voxel::BrickTag::empty) {
					continue;
				}
				for (int32_t lz = 0; lz < brick_dim; ++lz) {
					for (int32_t ly = 0; ly < brick_dim; ++ly) {
						for (int32_t lx = 0; lx < brick_dim; ++lx) {
							const glm::ivec3 v = brick * brick_dim + glm::ivec3(lx, ly, lz);
							const uint8_t palette_index = volume.materialAt(v);
							if (palette_index == voxel::k_empty_palette_index) {
								continue;
							}
							const uint32_t material_index = voxel::resolveMaterialIndex(palette, materials, palette_index);
							moments.add(v.x, v.y, v.z, materials.materials[material_index].density);
						}
					}
				}
			}
		}
	}
	return moments;
}
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

	PhysicsMaterial material;

	node.syncWorldTransform();
	const bool dynamic_body = not node.indestructible;
	const BodyID body = instance->createBody(
	    BodyDescriptor {
	      .type = dynamic_body ? BodyType::dynamic_body : BodyType::static_body,
	      .allow_sleep = node.allow_sleep,
	      .position = node.world_position,
	      .rotation = node.world_rotation,
	      .mass = 1.0f,
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
			instance->destroyFragmentsOf(binding.body);
			instance->destroyBody(binding.body);
		}
	}
	std::erase_if(instance->m_voxel_bindings, [&node](const VoxelNodeBinding& binding) {
		return binding.node.exists() && &*binding.node == &node;
	});
	node.assignBody({});
	node.assignShape({});

	std::scoped_lock voxel_lock {voxelDataMutex()};
	node.m_retired_volumes.clear();
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

	const auto tick_start = std::chrono::steady_clock::now();
	const auto elapsed_ms = [](std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to) {
		return std::chrono::duration<double, std::milli>(to - from).count();
	};

	const float dt = static_cast<float>(Accumulator::fixedDelta());
	reapFragments();
	syncEnabledState();
	applyDamageCommands();
	const auto after_damage = std::chrono::steady_clock::now();
	m_profile.damage_apply_ms = elapsed_ms(tick_start, after_damage);

	auto connectivity_results = runConnectivityAnalysis();
	const auto after_connectivity = std::chrono::steady_clock::now();
	m_profile.connectivity_ms = elapsed_ms(after_damage, after_connectivity);

	queuePendingFragments(connectivity_results);
	spawnBudgetedFragments();
	enforceFragmentBudget();
	despawnSettledFragments(dt);
	integrate(dt);

	{
		PhaseScope worker_phase {*this, SimulationPhase::mutation, SimulationPhase::worker_execution};
		const CollisionWorldView world {.bodies = m_bodies, .shapes = m_shapes, .voxel_shapes = m_voxel_shapes};
		const auto candidates = m_broad_phase.findPairs(world);
		m_manifolds = generateManifoldsAsync(world, candidates);
	}
	const auto after_narrow = std::chrono::steady_clock::now();
	m_profile.narrow_phase_ms = elapsed_ms(after_connectivity, after_narrow);

	updateCache(m_manifolds);
	wakeContactGroups();

	// resolve
	auto constraints = prepareConstraints(m_manifolds);
	auto islands = buildIslands(m_manifolds, constraints);
	solveIslands(islands);
	convertImpulsesToDamage(islands);
	updateSleeping(dt);
	m_profile.solve_ms = elapsed_ms(after_narrow, std::chrono::steady_clock::now());

	m_profile.manifold_count = m_manifolds.size();
	m_profile.voxel_shape_count = static_cast<size_t>(std::ranges::count_if(m_shapes, [](const ShapeSlot& s) {
		return s.occupied && s.shape.type == ShapeType::voxel;
	}));
	for (const BodySlot& b : m_bodies) {
		if (!b.occupied) {
			continue;
		}
		++m_profile.body_count;
		m_profile.awake_body_count += b.body.awake ? 1 : 0;
	}
	const voxel::BrickPool& pool = voxel::runtimeBrickPool();
	m_profile.brick_pool_allocated = pool.allocatedCount();
	m_profile.brick_pool_capacity = pool.capacity();

	// push poses after simulation settles
	publishTransforms();
	publishVoxelRenderRecords();
	m_profile.tick_ms = elapsed_ms(tick_start, std::chrono::steady_clock::now());
	publishProfile(islands);
	FrameMarkNamed("PhysicsStep");
}

void Simulator::publishProfile(std::span<const SimulationIsland> islands) {
	ZoneScopedN("physics::PublishProfile");
	(void)islands;
	m_published_profile = m_profile;
}

void Simulator::clearFragmentFromSource(
    ShapeID shape_id, VoxelShapeData& data, voxel::Volume& source, const DetachedComponent& component
) {
	ZoneScoped;

	std::vector<glm::ivec3> dirty_bricks;

	{
		std::scoped_lock voxel_lock {voxelDataMutex()};

		for (const auto& p : component.pieces) {
			dirty_bricks.emplace_back(p.brick);

			for (uint32_t i = 0; i < voxel::k_brick_voxel_count; ++i) {
				if (not voxel::isSolid(p.voxels, i)) {
					continue;
				}

				auto coords = voxel::localFromIndex(i);
				glm::ivec3 pos = p.brick * static_cast<int32_t>(voxel::k_brick_dim) + glm::ivec3(coords.x, coords.y, coords.z);
				uint8_t palette_index = source.materialAt(pos);
				uint32_t material_index = voxel::resolveMaterialIndex(data.palette, data.materials, palette_index);
				uint16_t density = data.materials.materials[material_index].density;

				if (source.setVoxel(pos, voxel::k_empty_palette_index).changed) {
					data.moments.remove(pos.x, pos.y, pos.z, density);
					data.solid_voxel_count -= data.solid_voxel_count > 0 ? 1u : 0u;
				}
			}
		}

		// Not tryCollapseUniform here since clearing a voxel only unfills a brick and never passes isFull
		data.surface.repairBricks(source, dirty_bricks);
	}

	// Carving does not bump the shape revision so its contacts stay warm
	++data.surface_revision;

	auto* shape = tryGetShape(shape_id);
	if (shape == nullptr) {
		return;
	}
	shape->voxel.local_bounds = computeOccupiedBounds(source);

	auto* body = tryGetBody(shape->owner);
	if (body == nullptr || body->type != BodyType::dynamic_body) {
		return;
	}
	if (source.solidVoxelCount() == 0) {
		retireVoxelBody(shape->owner);
		return;
	}
	rebuildMassProperties(shape->owner);
}

void Simulator::retireVoxelBody(BodyID id) {
	ZoneScopedN("physics::RetireVoxelBody");

	Body* body = tryGetBody(id);
	if (body == nullptr) {
		return;
	}

	body->enabled = false;
	body->awake = false;
	body->linear_velocity = {};
	body->angular_velocity = {};

	for (ShapeSlot& slot : m_shapes) {
		if (slot.occupied && slot.shape.owner == id) {
			slot.shape.enabled = false;

			if (slot.shape.type == ShapeType::voxel) {
				const VoxelShapeData* data = tryGetVoxelData(slot.shape.voxel.data);
				if (data != nullptr && data->fragment_sequence != 0) {
					m_doomed_fragments.push_back(id);
				}
			}
		}
	}
}

void Simulator::destroyFragmentsOf(BodyID origin) {
	ZoneScopedN("physics::DestroyFragmentsOf");

	if (not valid(origin)) {
		return;
	}

	std::vector<BodyID> doomed;
	for (const FragmentRecord& record : m_fragments) {
		const Shape* shape = tryGetShape(record.shape);
		const VoxelShapeData* data = shape != nullptr ? tryGetVoxelData(shape->voxel.data) : nullptr;
		if (data == nullptr || data->fragment_origin != origin) {
			continue;
		}
		doomed.push_back(record.body);
	}

	for (const BodyID body : doomed) {
		destroyFragmentRecord(body);
	}

	std::erase_if(m_pending_fragments, [this](const PendingFragments& pending) { return not valid(pending.shape); });
	rebuildFragmentIndex();
}

void Simulator::destroyFragmentRecord(BodyID id) {
	destroyBody(id);
	std::erase_if(m_fragments, [id](const FragmentRecord& record) { return record.body == id; });
}

void Simulator::rebuildFragmentIndex() {
	m_fragment_index.clear();
	m_fragment_index.reserve(m_fragments.size());
	for (size_t i = 0; i < m_fragments.size(); ++i) {
		m_fragment_index[m_fragments[i].body.slot] = i;
	}
}

void Simulator::reapFragments() {
	ZoneScopedN("physics::ReapFragments");

	for (const BodyID id : m_doomed_fragments) {
		destroyFragmentRecord(id);
	}
	m_doomed_fragments.clear();

	std::erase_if(m_fragments, [this](const FragmentRecord& record) { return not valid(record.body); });
	std::erase_if(m_pending_fragments, [this](const PendingFragments& pending) { return not valid(pending.shape); });

	// Picks least recently touched, not oldest created, so eviction never kills something still active
	while (not m_fragments.empty() && voxel::runtimeBrickPool().freeCount() < tunables().fragment_pool_headroom) {
		const auto oldest = std::ranges::min_element(m_fragments, {}, &FragmentRecord::sequence);
		destroyFragmentRecord(oldest->body);
		++m_profile.fragments_evicted;
	}

	rebuildFragmentIndex();
}

void Simulator::queuePendingFragments(std::span<const ConnectivityResult> results) {
	ZoneScopedN("physics::QueueFragments");

	for (const auto& r : results) {
		const Shape* shape = tryGetShape(r.shape);
		if (shape == nullptr || shape->type != ShapeType::voxel) {
			continue;
		}
		VoxelShapeData* data = tryGetVoxelData(shape->voxel.data);
		if (data == nullptr || data->volume == nullptr) {
			continue;
		}

		const glm::uvec3 brick_dims = data->volume->brickDims();
		const std::vector<ComponentClass> classes = classifyComponents(r.connectivity, brick_dims, data->anchor_mask);
		data->detached_components = buildDetachedComponents(r.connectivity, classes, brick_dims);

		// Debris sized pieces never become a tracked body
		std::vector<DetachedComponent> components_to_spawn;
		components_to_spawn.reserve(data->detached_components.size());
		for (const DetachedComponent& component : data->detached_components) {
			if (component.voxel_count < tunables().min_fragment_voxels) {
				clearFragmentFromSource(r.shape, *data, *data->volume, component);
				continue;
			}
			components_to_spawn.push_back(component);
		}

		const Body* source_body = tryGetBody(shape->owner);
		if (source_body != nullptr && source_body->type == BodyType::dynamic_body && not components_to_spawn.empty()) {
			components_to_spawn.erase(std::ranges::max_element(components_to_spawn, {}, &DetachedComponent::voxel_count));
		}

		const auto existing = std::ranges::find(m_pending_fragments, r.shape, &PendingFragments::shape);
		if (components_to_spawn.empty()) {
			if (existing != m_pending_fragments.end()) {
				m_pending_fragments.erase(existing);
			}
			continue;
		}

		if (existing != m_pending_fragments.end()) {
			*existing = PendingFragments {.shape = r.shape, .components = std::move(components_to_spawn), .cursor = 0};
		} else {
			m_pending_fragments.push_back(
			    PendingFragments {.shape = r.shape, .components = std::move(components_to_spawn), .cursor = 0}
			);
		}
	}
}

auto Simulator::reconcileComponent(const voxel::Volume& volume, const DetachedComponent& component) const -> bool {
	uint32_t surviving = 0;
	for (const voxel::BrickPiece& piece : component.pieces) {
		const voxel::BrickOccupancy* occupancy = volume.occupancyPointer(piece.brick);
		if (occupancy == nullptr) {
			continue;
		}
		surviving += voxel::popCount(*occupancy & piece.voxels);
	}
	return surviving == component.voxel_count;
}

void Simulator::spawnBudgetedFragments() {
	ZoneScopedN("physics::SpawnFragments");

	size_t spawned_this_step = 0;
	for (auto it = m_pending_fragments.begin();
	     it != m_pending_fragments.end() && spawned_this_step < tunables().max_fragment_spawns_per_step;) {
		PendingFragments& pending = *it;
		bool pool_full = false;

		while (pending.cursor < pending.components.size() && spawned_this_step < tunables().max_fragment_spawns_per_step) {
			const DetachedComponent& component = pending.components[pending.cursor];

			const Shape* shape = tryGetShape(pending.shape);
			const VoxelShapeData* data = shape != nullptr ? tryGetVoxelData(shape->voxel.data) : nullptr;
			if (data == nullptr || data->volume == nullptr || not reconcileComponent(*data->volume, component)) {
				++pending.cursor;
				continue;
			}

			if (not spawnFragmentBody(pending.shape, component)) {
				// false only means this component failed, not that the pool is full
				if (voxel::runtimeBrickPool().freeCount() == 0) {
					pool_full = true;
					break;
				}
				++pending.cursor;
				continue;
			}

			++pending.cursor;
			++spawned_this_step;
		}

		if (pool_full) {
			break;
		}

		if (pending.cursor >= pending.components.size()) {
			it = m_pending_fragments.erase(it);
		} else {
			++it;
		}
	}

	m_profile.fragments_pending = 0;
	for (const PendingFragments& pending : m_pending_fragments) {
		m_profile.fragments_pending += pending.components.size() - pending.cursor;
	}
}

void Simulator::enforceFragmentBudget() {
	ZoneScopedN("physics::FragmentProfile");

	size_t active = 0;
	for (const FragmentRecord& record : m_fragments) {
		const Body* body = tryGetBody(record.body);
		if (body != nullptr && body->enabled && body->awake) {
			++active;
		}
	}

	// only claim a fragment already about to sleep on its own
	for (const FragmentRecord& record : m_fragments) {
		if (active <= tunables().max_active_fragments) {
			break;
		}
		Body* body = tryGetBody(record.body);
		if (body == nullptr || not body->enabled || not body->awake) {
			continue;
		}
		const bool nearly_at_rest =
		    glm::dot(body->linear_velocity, body->linear_velocity) <
		        tunables().sleep_linear_threshold * tunables().sleep_linear_threshold * tunables().force_sleep_slack &&
		    glm::dot(body->angular_velocity, body->angular_velocity) <
		        tunables().sleep_angular_threshold * tunables().sleep_angular_threshold * tunables().force_sleep_slack;
		if (not nearly_at_rest) {
			continue;
		}
		body->sleep_locked = true;
		sleepBody(record.body);
		--active;
	}

	size_t sleep_locked = 0;
	for (const FragmentRecord& record : m_fragments) {
		const Body* body = tryGetBody(record.body);
		if (body != nullptr && body->sleep_locked) {
			++sleep_locked;
		}
	}
	m_profile.fragments_active = active;
	m_profile.fragments_sleep_locked = sleep_locked;
}

void Simulator::unlockSleep(BodyID id) {
	// no reordering, a rotate here used to cost O(m_fragments) per wake
	const auto it = m_fragment_index.find(id.slot);
	if (it == m_fragment_index.end() || it->second >= m_fragments.size() || m_fragments[it->second].body != id) {
		return;
	}

	if (Body* body = tryGetBody(id)) {
		body->sleep_locked = false;
	}
	wakeBody(id);

	m_fragments[it->second].sequence = m_next_fragment_sequence++;
}

auto Simulator::spawnFragmentBody(ShapeID source_shape_id, const DetachedComponent& component) -> bool {
	ZoneScopedN("physics::SpawnFragment");
	ZoneValue(static_cast<uint64_t>(component.voxel_count));

	Shape* shape = tryGetShape(source_shape_id);
	if (shape == nullptr) {
		return true;
	}
	VoxelShapeData* data = tryGetVoxelData(shape->voxel.data);
	if (data == nullptr || data->volume == nullptr) {
		return true;
	}
	Body* body = tryGetBody(shape->owner);
	if (body == nullptr) {
		return true;
	}

	glm::vec3 position = body->position;
	glm::quat rotation = body->rotation;
	glm::vec3 linear_velocity = body->linear_velocity;
	glm::vec3 angular_velocity = body->angular_velocity;
	glm::vec3 world_com = body->worldCenterOfMass();
	glm::vec3 local_center = shape->voxel.local_center;
	glm::quat local_rotation = shape->voxel.local_rotation;
	voxel::Palette palette = data->palette;
	voxel::MaterialLibrary materials = data->materials;
	const BodyID origin = valid(data->fragment_origin) ? data->fragment_origin : shape->owner;

	auto extracted = [&] {
		std::scoped_lock voxel_lock {voxelDataMutex()};
		return extractFragmentVolume(*data->volume, component);
	}();
	if (extracted.volume.solidVoxelCount() == 0) {
		++m_profile.fragment_spawn_failures;
		// setVoxel silently no ops when the runtime brick pool has no room left
		TOAST_WARN(
		    "Physics",
		    "Fragment body could not be extracted from shape {}: the runtime brick pool is out of bricks",
		    source_shape_id.slot
		);
		return false;
	}

	if (extracted.volume.solidVoxelCount() != component.voxel_count) {
		TOAST_WARN(
		    "Physics",
		    "Fragment extraction from shape {} ran out of pool bricks: expected {} voxels, got {}",
		    source_shape_id.slot,
		    component.voxel_count,
		    extracted.volume.solidVoxelCount()
		);
		return false;
	}

	const glm::uvec3 extracted_brick_dims = extracted.volume.brickDims();
	glm::vec3 origin_local = glm::vec3(extracted.offset) * static_cast<float>(voxel::k_brick_dim) * voxel::k_voxel_size;
	glm::quat frag_rotation = glm::normalize(rotation * local_rotation);
	glm::vec3 frag_pos = position + rotation * (local_center + local_rotation * origin_local);
	// clang-format off
	auto frag_body = createBody(BodyDescriptor {
		.type = BodyType::dynamic_body,
		.position = frag_pos,
		.rotation = frag_rotation,
		.angular_velocity = angular_velocity,
	});
	// clang-format on

	if (not valid(frag_body)) {
		return true;
	}

	const ShapeID frag_shape = createVoxelShape(frag_body, VoxelShape {}, std::move(extracted.volume), palette, materials);
	if (not valid(frag_shape)) {
		destroyBody(frag_body);
		return true;
	}
	if (const Shape* stored = tryGetShape(frag_shape)) {
		if (VoxelShapeData* frag_data = tryGetVoxelData(stored->voxel.data)) {
			frag_data->fragment_origin = origin;
			frag_data->fragment_sequence = m_next_fragment_sequence;
		}
	}
	m_fragment_index[frag_body.slot] = m_fragments.size();
	m_fragments.push_back(FragmentRecord {.body = frag_body, .shape = frag_shape, .sequence = m_next_fragment_sequence});
	++m_next_fragment_sequence;

	shape = tryGetShape(source_shape_id);
	if (shape != nullptr) {
		if (VoxelShapeData* source_data = tryGetVoxelData(shape->voxel.data)) {
			clearFragmentFromSource(source_shape_id, *source_data, *source_data->volume, component);
		}
	}

	rebuildMassProperties(frag_body);

	++m_profile.fragments_spawned;

	TOAST_TRACE(
	    "Physics",
	    "Fragment body {} broke off shape {}: {} voxels across {} pieces, extracted volume {}x{}x{} bricks ({}x{}x{} voxels, "
	    "{:.2f}x{:.2f}x{:.2f} m) at brick offset {} {} {}",
	    frag_body.slot,
	    source_shape_id.slot,
	    component.voxel_count,
	    component.pieces.size(),
	    extracted_brick_dims.x,
	    extracted_brick_dims.y,
	    extracted_brick_dims.z,
	    extracted_brick_dims.x * voxel::k_brick_dim,
	    extracted_brick_dims.y * voxel::k_brick_dim,
	    extracted_brick_dims.z * voxel::k_brick_dim,
	    static_cast<float>(extracted_brick_dims.x) * voxel::k_brick_size,
	    static_cast<float>(extracted_brick_dims.y) * voxel::k_brick_size,
	    static_cast<float>(extracted_brick_dims.z) * voxel::k_brick_size,
	    extracted.offset.x,
	    extracted.offset.y,
	    extracted.offset.z
	);

	if (auto* b = tryGetBody(frag_body)) {
		glm::vec3 frag_com_world = b->worldCenterOfMass();
		glm::vec3 r = frag_com_world - world_com;
		b->linear_velocity = linear_velocity + glm::cross(angular_velocity, r);
	}

	return true;
}

void Simulator::despawnSettledFragments(float dt) {
	ZoneScopedN("physics::DespawnSettledFragments");

	std::vector<BodyID> doomed;
	for (const auto& slot : m_shapes) {
		if (not slot.occupied || slot.shape.type != ShapeType::voxel) {
			continue;
		}
		VoxelShapeData* data = tryGetVoxelData(slot.shape.voxel.data);
		if (data == nullptr || not valid(data->fragment_origin)) {
			continue;
		}

		const Body* body = tryGetBody(slot.shape.owner);
		if (body == nullptr || not body->enabled || body->awake) {
			data->fragment_sleep_seconds = 0.0f;
			continue;
		}

		data->fragment_sleep_seconds += dt;
		if (data->solid_voxel_count <= tunables().fragment_despawn_max_voxels &&
		    data->fragment_sleep_seconds >= tunables().fragment_despawn_settle_seconds) {
			doomed.push_back(slot.shape.owner);
		}
	}

	for (const BodyID id : doomed) {
		destroyBody(id);
	}
	m_profile.fragments_despawned = doomed.size();
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
		} else if (binding.node.exists()) {
			binding.node->syncWorldTransform();
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
		for (ColliderBinding& collider_binding : binding.colliders) {
			Shape* shape = tryGetShape(collider_binding.shape);
			if (not shape) {
				continue;
			}

			const bool enabled = body->enabled && collider_binding.node.exists() && collider_binding.node->enabled() &&
			                     not collider_binding.node->disabled;
			setShapeEnabled(collider_binding.shape, enabled);
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
			binding.node->syncWorldTransform();
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
			{
				std::scoped_lock voxel_lock {voxelDataMutex()};
				binding.node->m_retired_volumes.clear();
			}
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
		if (body->sleep_locked) {
			return;
		}
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
		body->sleep_timer = tunables().sleep_delay;
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

void Simulator::wakeBodiesTouching(ShapeID id) {
	for (const CachedManifold& manifold : m_cached_manifolds) {
		if (manifold.pair.a.shape == id) {
			wakeBody(manifold.pair.b.body);
		} else if (manifold.pair.b.shape == id) {
			wakeBody(manifold.pair.a.body);
		}
	}
}

void Simulator::wakeContactGroups() {
	ZoneScopedN("physics::WakeContactGroups");

	std::vector<size_t> parents(m_bodies.size());
	std::vector<size_t> ranks(m_bodies.size(), 0);
	for (size_t index = 0; index < parents.size(); ++index) {
		parents[index] = index;
	}

	auto find_root = [&parents](size_t index) {
		size_t root = index;
		while (parents[root] != root) {
			root = parents[root];
		}
		while (parents[index] != index) {
			const size_t next = parents[index];
			parents[index] = root;
			index = next;
		}
		return root;
	};

	for (const Manifold& manifold : m_manifolds) {
		const Body* body_a = tryGetBody(manifold.pair.a.body);
		const Body* body_b = tryGetBody(manifold.pair.b.body);
		if (body_a == nullptr || body_b == nullptr || body_a->type != BodyType::dynamic_body ||
		    body_b->type != BodyType::dynamic_body) {
			continue;
		}

		size_t root_a = find_root(manifold.pair.a.body.slot);
		size_t root_b = find_root(manifold.pair.b.body.slot);
		if (root_a == root_b) {
			continue;
		}
		if (ranks[root_a] < ranks[root_b]) {
			std::swap(root_a, root_b);
		}
		parents[root_b] = root_a;
		if (ranks[root_a] == ranks[root_b]) {
			++ranks[root_a];
		}
	}

	std::vector<bool> group_is_awake(m_bodies.size(), false);
	for (size_t index = 0; index < m_bodies.size(); ++index) {
		const BodySlot& slot = m_bodies[index];
		if (slot.occupied && slot.body.enabled && slot.body.type == BodyType::dynamic_body && slot.body.awake) {
			group_is_awake[find_root(index)] = true;
		}
	}

	for (size_t index = 0; index < m_bodies.size(); ++index) {
		const BodySlot& slot = m_bodies[index];
		if (slot.occupied && slot.body.enabled && slot.body.type == BodyType::dynamic_body && not slot.body.sleep_locked &&
		    group_is_awake[find_root(index)]) {
			wakeBody(BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation});
		}
	}
}

void Simulator::setBodyTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation) {
	if (instance == nullptr) {
		return;
	}
	instance->setTransform(body, position, rotation);
}

auto Simulator::setBodyLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool {
	if (instance == nullptr) {
		return false;
	}
	return instance->setLinearVelocity(body, velocity);
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
			} else if (
			    const Body* owner = instance->tryGetBody(value->owner); owner != nullptr && owner->type != BodyType::dynamic_body
			) {
				instance->wakeBodiesTouching(shape);
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
			destroyBody(binding->body);
			binding = m_node_bindings.erase(binding);
			continue;
		}
		++binding;
	}

	for (auto binding = m_voxel_bindings.begin(); binding != m_voxel_bindings.end();) {
		if (not publishVoxelTransform(*binding)) {
			destroyBody(binding->body);
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

	if (const Shape* shape = tryGetShape(binding.shape); shape != nullptr && shape->type == ShapeType::voxel) {
		if (const VoxelShapeData* voxel_data = tryGetVoxelData(shape->voxel.data); voxel_data != nullptr) {
			binding.node->mass = voxel::resolveMass(voxel_data->moments);
		}
	}
	return true;
}

namespace {
inline constexpr uint64_t k_max_render_record_bricks = 1u << 22u;
}

void Simulator::publishVoxelRenderRecords() {
	ZoneScopedN("physics::PublishVoxelRenderRecords");

	std::scoped_lock voxel_lock {voxelDataMutex()};
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
		if (data->owned_volume == nullptr) {
			continue;
		}

		const glm::uvec3 record_dims = data->volume->brickDims();
		const uint64_t record_bricks =
		    static_cast<uint64_t>(record_dims.x) * static_cast<uint64_t>(record_dims.y) * static_cast<uint64_t>(record_dims.z);
		if (record_bricks == 0 || record_bricks > k_max_render_record_bricks) {
			TOAST_ERROR(
			    "Physics",
			    "Voxel shape {} reports {} x {} x {} bricks, refusing to publish it for rendering",
			    index,
			    record_dims.x,
			    record_dims.y,
			    record_dims.z
			);
			continue;
		}

		const glm::mat4 body_transform = glm::translate(glm::mat4(1.0f), body->position) * glm::mat4_cast(body->rotation);
		const glm::mat4 local_transform =
		    glm::translate(glm::mat4(1.0f), slot.shape.voxel.local_center) * glm::mat4_cast(slot.shape.voxel.local_rotation);

		m_voxel_render_records.push_back(
		    VoxelRenderRecord {
		      .shape = ShapeID {.slot = static_cast<uint32_t>(index), .generation = slot.generation},
		      .volume = data->volume,
		      .palette = &data->palette,
		      .transform = body_transform * local_transform,
		      .revision = data->surface_revision,
		      .fragment_origin = data->fragment_origin,
		      .awake = body->awake,
		      .world_bounds = worldShapeBounds(*body, slot.shape),
		}
		);
	}

	ZoneValue(static_cast<uint64_t>(m_voxel_render_records.size()));
}

auto Simulator::voxelFragmentRecords() -> std::span<const VoxelRenderRecord> {
	return instance != nullptr ? std::span<const VoxelRenderRecord> {instance->m_voxel_render_records}
	                           : std::span<const VoxelRenderRecord> {};
}

auto Simulator::stepProfile() -> const PhysicsStepProfile& {
	static const PhysicsStepProfile empty;
	return instance != nullptr ? instance->m_published_profile : empty;
}

void Simulator::recordTickBurst(size_t steps, bool time_budget_reached) {
	if (instance != nullptr) {
		instance->m_published_profile.ticks_this_frame = steps;
		instance->m_published_profile.ticks_capped_by_time_budget = time_budget_reached;
	}
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
	if (body_a.inverse_mass > 0.0f) {
		body_a.linear_velocity -= impulse * body_a.inverse_mass;
		body_a.angular_velocity -= body_a.inverse_inertia_world * glm::cross(r_a, impulse);
	}
	if (body_b.inverse_mass > 0.0f) {
		body_b.linear_velocity += impulse * body_b.inverse_mass;
		body_b.angular_velocity += body_b.inverse_inertia_world * glm::cross(r_b, impulse);
	}
}

// No ZoneScoped below this runs hundreds of thousands of times a tick and the push costs more than the math
auto Simulator::solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool {
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
		float excess_penetration = std::max(deepest_penetration - tunables().penetration_slop, 0.0f);
		if (excess_penetration == 0.0f) {
			continue;
		}

		float correction_distance = std::min(tunables().correction_beta * excess_penetration, tunables().max_correction);
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

auto Simulator::pairNeedsNarrowPhase(CollisionWorldView world, const BroadPhasePair& pair) const -> bool {
	const Body* body_a = world.body(pair.a.body);
	const Body* body_b = world.body(pair.b.body);
	const bool a_is_active = body_a && body_a->type == BodyType::dynamic_body && body_a->enabled && body_a->awake;
	const bool b_is_active = body_b && body_b->type == BodyType::dynamic_body && body_b->enabled && body_b->awake;
	return a_is_active || b_is_active;
}

auto Simulator::generateManifoldsAsync(CollisionWorldView world, std::span<const BroadPhasePair> candidates)
    -> std::vector<Manifold> {
	ZoneScopedN("physics::NarrowPhase");

	std::vector<BroadPhasePair> active_candidates;
	active_candidates.reserve(candidates.size());
	for (const BroadPhasePair& pair : candidates) {
		if (pairNeedsNarrowPhase(world, pair)) {
			active_candidates.emplace_back(pair);
		} else {
			++m_profile.sleeping_pairs_skipped;
		}
	}

	const size_t minimum_candidates_per_job = tunables().min_candidates_per_job;
	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), static_cast<size_t>(1));
	const size_t maximum_job_count = worker_count * 3;
	const size_t job_count =
	    active_candidates.empty()
	        ? 0
	        : std::min(maximum_job_count, std::max(active_candidates.size() / minimum_candidates_per_job, size_t {1}));

	std::vector<std::future<ManifoldQueue>> futures;
	futures.reserve(job_count);

	for (size_t job_index = 0; job_index < job_count; ++job_index) {
		const size_t begin = job_index * active_candidates.size() / job_count;
		const size_t end = (job_index + 1) * active_candidates.size() / job_count;
		auto batch = std::span<const BroadPhasePair> {active_candidates}.subspan(begin, end - begin);

		futures.emplace_back(toast::ThreadPool::push([this, world, batch] {
			ZoneScopedN("physics::NarrowPhaseBatch");
			ZoneValue(static_cast<uint64_t>(batch.size()));
			return m_narrow_phase.generateManifolds(world, batch);
		}));
	}
	m_profile.narrow_jobs = futures.size();
	m_profile.narrow_candidates = active_candidates.size();

	std::vector<Manifold> merged;
	merged.reserve(active_candidates.size());

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
		                      linear_speed_squared <= tunables().sleep_linear_threshold * tunables().sleep_linear_threshold &&
		                      angular_speed_squared <= tunables().sleep_angular_threshold * tunables().sleep_angular_threshold;
		if (is_still) {
			body.sleep_timer = std::min(body.sleep_timer + dt, tunables().sleep_delay);
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
		group_can_sleep[root] = group_can_sleep[root] && body.allow_sleep && body.sleep_timer >= tunables().sleep_delay;
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
		const CachedManifoldKey key {.pair = manifold.pair, .normal_index = manifold.normal_index};
		const auto old_manifold = std::lower_bound(
		    m_cached_manifolds.begin(), m_cached_manifolds.end(), key, [](const CachedManifold& cached, CachedManifoldKey candidate) {
			    return cachedManifoldLess(cached, candidate);
		    }
		);
		const bool found_old = old_manifold != m_cached_manifolds.end() && cachedManifoldMatches(*old_manifold, key);
		const bool revisions_match =
		    found_old && old_manifold->shape_a_revision == revision_a && old_manifold->shape_b_revision == revision_b;

		if (revisions_match) {
			++m_profile.contact_persists;
			event::send<event::ContactPersist>(manifold);
		} else {
			wakeBody(manifold.pair.a.body);
			wakeBody(manifold.pair.b.body);
			if (found_old) {
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

			const std::span<const CachedContact> old_contacts(old_manifold->contacts.data(), old_manifold->contact_count);
			const auto old_contact = std::ranges::find_if(old_contacts, [&current_contact](const CachedContact& cached) {
				return cached.feature_a == current_contact.feature_a && cached.feature_b == current_contact.feature_b;
			});
			if (old_contact != old_contacts.end()) {
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
		const CachedManifoldKey key {.pair = cached.pair, .normal_index = cached.normal_index};
		const auto current =
		    std::lower_bound(manifolds.begin(), manifolds.end(), key, [](const Manifold& manifold, CachedManifoldKey candidate) {
			    return manifold.pair < candidate.pair ||
					       (manifold.pair == candidate.pair && manifold.normal_index < candidate.normal_index);
		    });
		const bool still_colliding =
		    current != manifolds.end() && current->pair == cached.pair && current->normal_index == cached.normal_index;
		if (still_colliding) {
			continue;
		}

		const Body* body_a = tryGetBody(cached.pair.a.body);
		const Body* body_b = tryGetBody(cached.pair.b.body);
		const bool a_active = body_a && body_a->type == BodyType::dynamic_body && body_a->enabled && body_a->awake;
		const bool b_active = body_b && body_b->type == BodyType::dynamic_body && body_b->enabled && body_b->awake;
		if (body_a && body_b && not a_active && not b_active) {
			next_cache.emplace_back(cached);
			continue;
		}

		wakeBody(cached.pair.a.body);
		wakeBody(cached.pair.b.body);
		++m_profile.contact_ends;
		event::send<event::ContactEnd>(cached.pair);
	}

	std::ranges::sort(next_cache, [](const CachedManifold& lhs, const CachedManifold& rhs) {
		return cachedManifoldLess(lhs, rhs);
	});
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

		integrateBody(
		    BodyID {.slot = static_cast<uint32_t>(index), .generation = slot.generation}, slot.body, tunables().gravity, dt
		);
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
	if (body.lock_position.x) {
		body.linear_velocity.x = 0.0f;
	}
	if (body.lock_position.y) {
		body.linear_velocity.y = 0.0f;
	}
	if (body.lock_position.z) {
		body.linear_velocity.z = 0.0f;
	}
	const glm::vec3 center_of_mass = body.worldCenterOfMass() + body.linear_velocity * dt;

	// angular integration
	if (body.lock_rotation.x) {
		body.angular_velocity.x = 0.0f;
	}
	if (body.lock_rotation.y) {
		body.angular_velocity.y = 0.0f;
	}
	if (body.lock_rotation.z) {
		body.angular_velocity.z = 0.0f;
	}
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
	body.position = center_of_mass - body.rotation * body.local_center_of_mass;
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
	  .gravity_scale = descriptor.gravity_scale,
	  .lock_position = descriptor.lock_position,
	  .lock_rotation = descriptor.lock_rotation
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
    BodyID owner, const VoxelShape& shape, voxel::Volume& volume, const voxel::Palette& palette,
    const voxel::MaterialLibrary& materials
) -> ShapeID {
	return createVoxelShapeInternal(owner, shape, &volume, nullptr, palette, materials);
}

auto Simulator::createVoxelShape(
    BodyID owner, const VoxelShape& shape, voxel::Volume&& volume, const voxel::Palette& palette,
    const voxel::MaterialLibrary& materials
) -> ShapeID {
	return createVoxelShapeInternal(owner, shape, nullptr, std::make_unique<voxel::Volume>(std::move(volume)), palette, materials);
}

auto Simulator::createVoxelShapeInternal(
    BodyID owner, const VoxelShape& shape, voxel::Volume* external, std::unique_ptr<voxel::Volume> owned,
    const voxel::Palette& palette, const voxel::MaterialLibrary& materials
) -> ShapeID {
	ZoneScopedN("physics::CreateVoxelShape");
	ZoneValue(static_cast<uint64_t>(owner.slot));
	if (not mainThreadMutationAllowed()) {
		return {};
	}

	if ((owned != nullptr) == (external != nullptr)) {
		TOAST_WARN("Physics", "Rejected voxel shape that names neither exactly one owned nor one borrowed volume");
		return {};
	}
	voxel::Volume& volume = owned != nullptr ? *owned : *external;

	if (not valid(owner)) {
		TOAST_WARN("Physics", "Rejected voxel shape registration for an invalid body");
		return {};
	}

	const bool center_is_finite =
	    std::isfinite(shape.local_center.x) && std::isfinite(shape.local_center.y) && std::isfinite(shape.local_center.z);
	const float rotation_length_squared = glm::dot(shape.local_rotation, shape.local_rotation);
	const glm::uvec3 brick_dims = volume.brickDims();
	if (not center_is_finite || not std::isfinite(rotation_length_squared) || rotation_length_squared <= 1.0e-10f ||
	    brick_dims.x == 0 || brick_dims.y == 0 || brick_dims.z == 0) {
		TOAST_WARN("Physics", "Rejected voxel shape with invalid dimensions, local center, or local rotation");
		return {};
	}

	if (not voxel::validateLibrary(materials).empty() || not voxel::validatePalette(palette, materials).empty()) {
		TOAST_WARN("Physics", "Rejected voxel shape with an invalid palette or material library");
		return {};
	}

	voxel::VolumeSurface surface;
	voxel::MassMoments moments;
	{
		std::scoped_lock voxel_lock {voxelDataMutex()};
		surface.rebuild(volume);
		moments = accumulateMassMoments(volume, palette, materials);
	}

	const AnchorMask default_anchor_mask = tryGetBody(owner)->type == BodyType::static_body ? k_anchor_bottom : k_anchor_null;

	VoxelShapeData voxel_data {
	  .volume = &volume,
	  .surface = std::move(surface),
	  .moments = moments,
	  .palette = palette,
	  .materials = materials,
	  .solid_voxel_count = volume.solidVoxelCount(),
	  .anchor_mask = default_anchor_mask,
	  .owned_volume = std::move(owned),
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
	stored_shape.local_bounds = computeOccupiedBounds(volume);

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
	if (model == nullptr || palette == nullptr) {
		TOAST_WARN("Physics", "Voxel node '{}' is missing a valid model or palette", node.name());
		return {};
	}

	// TODO:
	const voxel::MaterialLibrary* materials = node.resolvedMaterialLibrary();
	if (materials == nullptr) {
		materials = &placeholderMaterialLibrary();
	}

	node.syncTransform();
	const bool has_unit_scale =
	    glm::all(glm::lessThanEqual(glm::abs(node.world_scale - glm::vec3(1.0f)), glm::vec3(unit_scale_tolerance)));
	if (not has_unit_scale) {
		TOAST_WARN("Physics", "Voxel node '{}' cannot register with non-unit world scale", node.name());
		return {};
	}

	voxel::Volume* volume = nullptr;
	{
		std::scoped_lock voxel_lock {voxelDataMutex()};
		volume = node.volume();
	}
	if (volume == nullptr) {
		TOAST_WARN("Physics", "Voxel node '{}' has no instantiated volume to collide against", node.name());
		return {};
	}

	const VoxelShape voxel_shape;

	const ShapeID shape = createVoxelShape(owner, voxel_shape, *volume, *palette, *materials);
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
		{
			std::scoped_lock voxel_lock {voxelDataMutex()};
			std::erase_if(m_voxel_render_records, [shape](const VoxelRenderRecord& record) { return record.shape == shape; });
		}
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

	std::scoped_lock voxel_lock {voxelDataMutex()};
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

	const glm::vec3 previous_center_of_mass = body->local_center_of_mass;
	body->inverse_inertia_local = {0.0f};
	body->inverse_inertia_world = {0.0f};
	body->local_center_of_mass = {};
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
	auto* shape = tryGetShape(shapes[0]);
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
			const VoxelShapeData* voxel_data = tryGetVoxelData(shape->voxel.data);
			if (voxel_data == nullptr || voxel_data->volume == nullptr) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid voxel data");
				return;
			}

			if (voxel_data->moments.isEmpty()) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: voxel shape has no solid voxels");
				return;
			}

			const voxel::MassProperties properties = voxel::resolve(voxel_data->moments);
			if (not std::isfinite(properties.mass) || properties.mass <= 0.0f || not std::isfinite(properties.inertia[0][0]) ||
			    not std::isfinite(properties.inertia[1][1]) || not std::isfinite(properties.inertia[2][2])) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: invalid voxel mass properties");
				return;
			}

			const glm::mat3 local_rotation = glm::mat3_cast(shape->voxel.local_rotation);
			const glm::mat3 inertia = local_rotation * properties.inertia * glm::transpose(local_rotation);
			const glm::mat3 inverse_inertia = glm::inverse(inertia);
			if (not std::isfinite(inverse_inertia[0][0]) || not std::isfinite(inverse_inertia[1][1]) ||
			    not std::isfinite(inverse_inertia[2][2])) {
				TOAST_WARN("Physics", "rebuildMassProperties() was aborted: singular voxel inertia");
				return;
			}

			body->inverse_mass = 1.0f / properties.mass;
			body->local_center_of_mass = shape->voxel.local_center + shape->voxel.local_rotation * properties.center_of_mass;
			body->inverse_inertia_local = inverse_inertia;
			const glm::mat3 rotation = glm::mat3_cast(body->rotation);
			body->inverse_inertia_world = rotation * body->inverse_inertia_local * glm::transpose(rotation);
			break;
		}
		case ShapeType::capsule: {
			// Conservative approximation using a box with dimensions (2r, 2r, height)
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
			return;
		}
	}

	const glm::vec3 center_shift = body->rotation * (body->local_center_of_mass - previous_center_of_mass);
	body->linear_velocity += glm::cross(body->angular_velocity, center_shift);
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

auto Simulator::shapeWorldBounds(ShapeID shape) -> std::optional<AABB> {
	if (not instance) {
		return std::nullopt;
	}

	const Shape* value = instance->tryGetShape(shape);
	if (not value) {
		return std::nullopt;
	}

	const Body* body = instance->tryGetBody(value->owner);
	if (not body) {
		return std::nullopt;
	}

	return worldShapeBounds(*body, *value);
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

void Simulator::recordDamage(DamageCommand&& command) {
	ZoneScopedN("physics::RecordDamage");
	std::scoped_lock lock {m_damage_mutex};
	m_damage_commands.emplace_back(command);
}

void Simulator::applyDamageCommands() {
	ZoneScopedN("physics::ApplyDamageCommands");
	m_debug_dirty_bricks.clear();

	std::vector<DamageCommand> c;
	{
		std::scoped_lock lock {m_damage_mutex};
		c = std::move(m_damage_commands);
		m_damage_commands.clear();
	}

	ZoneValue(static_cast<uint64_t>(c.size()));
	m_profile.damage_commands = c.size();

	for (const auto& command : c) {
		applyDamageCommand(command);
	}
}

void Simulator::applyDamageCommand(const DamageCommand& c) {
	ZoneScopedN("physics::ApplyDamage");

	if (not mainThreadMutationAllowed()) {
		return;
	}
	if (not std::isfinite(c.radius) || c.radius <= 0.0f) {
		return;
	}

	Shape* shape = tryGetShape(c.shape);
	if (shape == nullptr || shape->type != ShapeType::voxel) {
		return;
	}
	VoxelShapeData* data = tryGetVoxelData(shape->voxel.data);
	if (data == nullptr || data->volume == nullptr) {
		return;
	}
	Body* body = tryGetBody(shape->owner);
	if (body == nullptr) {
		return;
	}

	const auto binding = std::ranges::find(m_voxel_bindings, c.shape, &VoxelNodeBinding::shape);
	if (binding != m_voxel_bindings.end() && binding->node.exists() && binding->node->indestructible) {
		return;
	}

	// we get the position in volume space
	glm::vec3 in_body_frame = glm::inverse(body->rotation) * (c.world_center - body->position);
	glm::vec3 local_center = glm::inverse(shape->voxel.local_rotation) * (in_body_frame - shape->voxel.local_center);

	const float shell = glm::max(c.shell_voxels, 0.0f) * voxel::k_voxel_size;
	const float inner_radius = shell > 0.0f ? glm::max(c.radius - shell, 0.0f) : 0.0f;
	const float outer_squared = c.radius * c.radius;
	const float inner_squared = inner_radius * inner_radius;

	voxel::Volume& volume = *data->volume;
	const glm::ivec3 voxel_dims = glm::ivec3(volume.voxelDims());

	glm::ivec3 first = glm::ivec3(glm::floor((local_center - glm::vec3(c.radius)) / voxel::k_voxel_size));
	glm::ivec3 last = glm::ivec3(glm::ceil((local_center + glm::vec3(c.radius)) / voxel::k_voxel_size)) - 1;
	first = glm::clamp(first, glm::ivec3(0), voxel_dims - 1);
	last = glm::clamp(last, glm::ivec3(0), voxel_dims - 1);
	if (first.x > last.x || first.y > last.y || first.z > last.z) {
		return;
	}

	const auto brick_dim = static_cast<int32_t>(voxel::k_brick_dim);
	const glm::ivec3 first_brick = first / brick_dim;
	const glm::ivec3 last_brick = last / brick_dim;

	std::vector<glm::ivec3> dirty_bricks;

	{
		std::scoped_lock voxel_lock {voxelDataMutex()};

		for (int32_t bz = first_brick.z; bz <= last_brick.z; ++bz) {
			for (int32_t by = first_brick.y; by <= last_brick.y; ++by) {
				for (int32_t bx = first_brick.x; bx <= last_brick.x; ++bx) {
					const glm::ivec3 brick {bx, by, bz};
					if (volume.entryAt(brick).tag() == voxel::BrickTag::empty) {
						continue;
					}

					bool brick_dirty = false;
					const glm::ivec3 brick_first = glm::max(first, brick * brick_dim);
					const glm::ivec3 brick_last = glm::min(last, brick * brick_dim + (brick_dim - 1));
					for (int32_t z = brick_first.z; z <= brick_last.z; ++z) {
						for (int32_t y = brick_first.y; y <= brick_last.y; ++y) {
							for (int32_t x = brick_first.x; x <= brick_last.x; ++x) {
								const glm::ivec3 v {x, y, z};

								const uint8_t palette_index = volume.materialAt(v);
								if (palette_index == voxel::k_empty_palette_index) {
									continue;
								}

								const glm::vec3 voxel_center = (glm::vec3 {v} + 0.5f) * voxel::k_voxel_size;
								const glm::vec3 offset = voxel_center - local_center;
								const float distance_squared = glm::dot(offset, offset);
								if (distance_squared > outer_squared || distance_squared < inner_squared) {
									continue;
								}

								const uint32_t material_index = voxel::resolveMaterialIndex(data->palette, data->materials, palette_index);
								const voxel::PhysicalMaterial& material = data->materials.materials[material_index];
								if (material.isIndestructible() || material.toughness > c.energy) {
									continue;
								}

								voxel::Volume::VoxelWrite write = volume.setVoxel(v, voxel::k_empty_palette_index);
								if (write.changed) {
									data->moments.remove(v.x, v.y, v.z, material.density);
									data->solid_voxel_count -= data->solid_voxel_count > 0 ? 1u : 0u;
									brick_dirty = true;
									data->connectivity_dirty = true;
								}
							}
						}
					}

					if (brick_dirty) {
						dirty_bricks.push_back(brick);
					}
				}
			}
		}

		if (dirty_bricks.empty()) {
			return;
		}

		data->surface.repairBricks(volume, dirty_bricks);
		for (const glm::ivec3& brick : dirty_bricks) {
			m_debug_dirty_bricks.push_back({.shape = c.shape, .brick = brick});
		}
	}

	ZoneValue(static_cast<uint64_t>(dirty_bricks.size()));
	m_profile.dirty_bricks += dirty_bricks.size();
	m_profile.surface_bricks_repaired += dirty_bricks.size();

	// Carving does not bump the shape revision so its contacts stay warm
	++data->surface_revision;
	shape->voxel.local_bounds = computeOccupiedBounds(volume);

	for (VoxelNodeBinding& binding : m_voxel_bindings) {
		if (binding.shape != c.shape || not binding.node.exists()) {
			continue;
		}
		++binding.node->m_revision;
		binding.source_revision = binding.node->revision();
		break;
	}

	if (body->type == BodyType::dynamic_body) {
		if (volume.solidVoxelCount() == 0) {
			retireVoxelBody(shape->owner);
		} else {
			rebuildMassProperties(shape->owner);
		}
	}

	unlockSleep(shape->owner);

	wakeBodiesInBounds(
	    AABB {
	      .min = c.world_center - glm::vec3(c.radius),
	      .max = c.world_center + glm::vec3(c.radius),
	    }
	);
}

void Simulator::wakeBodiesInBounds(const AABB& bounds) {
	ZoneScopedN("physics::WakeBodiesInBounds");

	for (const ShapeID shape_id : m_broad_phase.queryBounds(bounds)) {
		if (const Shape* shape = tryGetShape(shape_id)) {
			wakeBody(shape->owner);
		}
	}
}

void Simulator::applyExplosion(const glm::vec3& position, float radius, float energy) {
	ZoneScopedN("physics::ApplyExplosion");

	if (not mainThreadMutationAllowed()) {
		return;
	}
	if (not std::isfinite(radius) || radius <= 0.0f || not std::isfinite(energy)) {
		return;
	}

	const AABB bounds {.min = position - glm::vec3(radius), .max = position + glm::vec3(radius)};
	for (const ShapeID shape_id : m_broad_phase.queryBounds(bounds)) {
		const Shape* shape = tryGetShape(shape_id);
		if (shape == nullptr || shape->type != ShapeType::voxel || not shape->enabled) {
			continue;
		}

		recordDamage(
		    DamageCommand {
		      .shape = shape_id,
		      .world_center = position,
		      .radius = radius,
		      .energy = energy,
		    }
		);
	}
}

auto Simulator::shootVoxel(
    const glm::vec3& origin, const glm::vec3& direction, float max_distance, float energy, float min_radius
) -> bool {
	ZoneScopedN("physics::ShootVoxel");

	if (not mainThreadMutationAllowed()) {
		return false;
	}
	if (not std::isfinite(max_distance) || max_distance <= 0.0f || not std::isfinite(energy)) {
		return false;
	}

	const float length = glm::length(direction);
	if (not std::isfinite(length) || length <= 1.0e-6f) {
		return false;
	}
	const glm::vec3 dir = direction / length;
	const glm::vec3 inv_dir = 1.0f / dir;
	const glm::vec3 end = origin + dir * max_distance;
	const AABB sweep_bounds {.min = glm::min(origin, end), .max = glm::max(origin, end)};

	struct Candidate {
		ShapeID shape;
		float t_min;
		float t_max;
	};

	std::vector<Candidate> candidates;

	for (const ShapeID shape_id : m_broad_phase.queryBounds(sweep_bounds)) {
		const Shape* shape = tryGetShape(shape_id);
		if (shape == nullptr || shape->type != ShapeType::voxel || not shape->enabled) {
			continue;
		}
		const Body* body = tryGetBody(shape->owner);
		if (body == nullptr) {
			continue;
		}
		const std::optional<AABB::RayHit> hit = worldShapeBounds(*body, *shape).intersectRay(origin, inv_dir, max_distance);
		if (not hit) {
			continue;
		}
		candidates.push_back(Candidate {.shape = shape_id, .t_min = hit->t_min, .t_max = hit->t_max});
	}

	if (candidates.empty()) {
		return false;
	}

	std::ranges::sort(candidates, {}, &Candidate::t_min);

	const float march_step = voxel::k_voxel_size * 0.25f;

	for (const Candidate& candidate : candidates) {
		const Shape* shape = tryGetShape(candidate.shape);
		if (shape == nullptr) {
			continue;
		}
		VoxelShapeData* data = tryGetVoxelData(shape->voxel.data);
		if (data == nullptr || data->volume == nullptr) {
			continue;
		}
		const Body* body = tryGetBody(shape->owner);
		if (body == nullptr) {
			continue;
		}

		const glm::quat inv_body_rotation = glm::inverse(body->rotation);
		const glm::quat inv_local_rotation = glm::inverse(shape->voxel.local_rotation);
		const glm::vec3 origin_local =
		    inv_local_rotation * (inv_body_rotation * (origin - body->position) - shape->voxel.local_center);
		const glm::vec3 dir_local = inv_local_rotation * (inv_body_rotation * dir);

		const glm::ivec3 voxel_dims = glm::ivec3(data->volume->voxelDims());
		const float t_end = std::min(candidate.t_max, max_distance);

		for (float t = std::max(candidate.t_min, 0.0f); t <= t_end; t += march_step) {
			const glm::vec3 local_point = origin_local + dir_local * t;
			const glm::ivec3 voxel_coord = glm::ivec3(glm::floor(local_point / voxel::k_voxel_size));
			if (glm::any(glm::lessThan(voxel_coord, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(voxel_coord, voxel_dims))) {
				continue;
			}
			const uint8_t palette_index = data->volume->materialAt(voxel_coord);
			if (palette_index == voxel::k_empty_palette_index) {
				continue;
			}

			const glm::vec3 hit_point = origin + dir * t;
			const uint32_t material_index = voxel::resolveMaterialIndex(data->palette, data->materials, palette_index);
			const voxel::PhysicalMaterial& material = data->materials.materials[material_index];
			const float final_radius = std::max({material.shatter_radius, voxel::k_voxel_size, min_radius});

			recordDamage(
			    DamageCommand {
			      .shape = candidate.shape,
			      .world_center = hit_point,
			      .radius = final_radius,
			      .energy = energy,
			      .shell_voxels = 2.0f,
			    }
			);
			return true;
		}
	}

	return false;
}

auto Simulator::runConnectivityAnalysis() -> std::vector<ConnectivityResult> {
	ZoneScopedN("physics::ConnectivityAnalysis");

	struct PendingJob {
		ShapeID shape;
		uint32_t revision;
		std::future<voxel::Connectivity> future;
	};

	std::vector<PendingJob> pending;

	std::scoped_lock voxel_lock {voxelDataMutex()};

	// A rotating start point so a sustained overload does not always starve the same high index shapes
	const size_t shape_count = m_shapes.size();
	for (size_t scanned = 0, index = shape_count == 0 ? 0 : m_connectivity_cursor % shape_count;
	     scanned < shape_count && pending.size() < tunables().max_connectivity_jobs_per_tick;
	     ++scanned, index = (index + 1) % shape_count) {
		const ShapeSlot& slot = m_shapes[index];
		if (not slot.occupied || slot.shape.type != ShapeType::voxel) {
			continue;
		}

		auto* data = tryGetVoxelData(slot.shape.voxel.data);
		if (not data || not data->connectivity_dirty) {
			continue;
		}

		auto* volume = data->volume;
		if (volume == nullptr) {
			continue;
		}

		ShapeID id {.slot = static_cast<uint32_t>(index), .generation = slot.generation};
		uint32_t revision = data->surface_revision;
		data->connectivity_dirty = false;

		// clang-format off
		pending.emplace_back(PendingJob {
			.shape = id,
			.revision = revision,
			.future = toast::ThreadPool::push([volume] {
				ZoneScopedN("physics::ConnectivityBatch");
				return voxel::analyseConnectivity(*volume);
			})
		});
		// clang-format on

		m_connectivity_cursor = (index + 1) % shape_count;
	}
	m_profile.connectivity_jobs_dispatched = pending.size();

	ZoneValue(static_cast<uint64_t>(pending.size()));
	m_profile.connectivity_jobs = pending.size();

	std::vector<ConnectivityResult> results;
	results.reserve(pending.size());
	{
		ZoneScopedNC("physics::ConnectivityAwait", 0x202020);
		for (auto& p : pending) {
			auto c = p.future.get();
			const Shape* shape = tryGetShape(p.shape);
			VoxelShapeData* data = shape != nullptr && shape->type == ShapeType::voxel ? tryGetVoxelData(shape->voxel.data) : nullptr;
			if (data == nullptr || data->surface_revision != p.revision) {
				// The volume changed mid compute so this result no longer matches it
				if (data != nullptr) {
					data->connectivity_dirty = true;
				}
				++m_profile.connectivity_jobs_stale;
				continue;
			}
			results.emplace_back(ConnectivityResult {.shape = p.shape, .connectivity = std::move(c)});
		}
	}

	m_profile.connectivity_shapes_waiting = static_cast<size_t>(std::ranges::count_if(m_shapes, [this](const ShapeSlot& s) {
		if (not s.occupied || s.shape.type != ShapeType::voxel) {
			return false;
		}
		const auto* data = tryGetVoxelData(s.shape.voxel.data);
		return data != nullptr && data->connectivity_dirty;
	}));

	return results;
}

void Simulator::convertImpulsesToDamage(std::span<const SimulationIsland> islands) {
	ZoneScopedN("physics::ConvertImpulsesToDamage");

	std::scoped_lock voxel_lock {voxelDataMutex()};

	for (const SimulationIsland& island : islands) {
		for (const Constraint& constraint : island.constraints) {
			const float impulse = constraint.accumulated_normal_impulse;
			if (not std::isfinite(impulse) || impulse <= 0.0f) {
				continue;
			}

			const Shape* shape_a = tryGetShape(constraint.pair.a.shape);
			const Shape* shape_b = tryGetShape(constraint.pair.b.shape);
			if (shape_a == nullptr || shape_b == nullptr) {
				continue;
			}

			const bool voxel_is_a = shape_a->type == ShapeType::voxel;
			if (not voxel_is_a && shape_b->type != ShapeType::voxel) {
				continue;
			}

			const Shape& voxel_shape = voxel_is_a ? *shape_a : *shape_b;
			const ContactFeatureID feature = voxel_is_a ? constraint.feature_a : constraint.feature_b;
			const auto feature_type = static_cast<FeatureType>(feature.value >> 56);
			if (feature_type != FeatureType::voxel_face && feature_type != FeatureType::voxel_edge) {
				continue;
			}

			const VoxelShapeData* data = tryGetVoxelData(voxel_shape.voxel.data);
			if (data == nullptr || data->volume == nullptr) {
				continue;
			}

			const glm::ivec3 brick_dims = glm::ivec3(data->volume->brickDims());
			if (brick_dims.x <= 0 || brick_dims.y <= 0 || brick_dims.z <= 0) {
				continue;
			}
			const VoxelFeaturePayload payload = unpackVoxelFeature(feature);
			const auto slice = static_cast<uint32_t>(brick_dims.x * brick_dims.y);
			const glm::ivec3 brick {
			  static_cast<int32_t>(payload.slot % static_cast<uint32_t>(brick_dims.x)),
			  static_cast<int32_t>((payload.slot / static_cast<uint32_t>(brick_dims.x)) % static_cast<uint32_t>(brick_dims.y)),
			  static_cast<int32_t>(payload.slot / slice),
			};
			const voxel::BrickCoord local = voxel::localFromIndex(payload.local_index);
			const glm::ivec3 hit_voxel =
			    brick * static_cast<int32_t>(voxel::k_brick_dim) +
			    glm::ivec3(static_cast<int32_t>(local.x), static_cast<int32_t>(local.y), static_cast<int32_t>(local.z));

			const uint8_t palette_index = data->volume->materialAt(hit_voxel);
			if (palette_index == voxel::k_empty_palette_index) {
				continue;
			}

			const uint32_t material_index = voxel::resolveMaterialIndex(data->palette, data->materials, palette_index);
			const voxel::PhysicalMaterial& material = data->materials.materials[material_index];
			if (material.isIndestructible() || material.shatter_radius <= 0.0f || impulse <= material.toughness) {
				continue;
			}

			recordDamage(
			    DamageCommand {
			      .shape = voxel_is_a ? constraint.pair.a.shape : constraint.pair.b.shape,
			      .world_center = constraint.contact_point,
			      .radius = material.shatter_radius,
			      .energy = impulse,
			      .source = voxel_is_a ? constraint.body_b : constraint.body_a,
			    }
			);
		}
	}
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

	const CachedManifoldKey key {.pair = pair, .normal_index = normal_index};
	const auto manifold = std::lower_bound(
	    m_cached_manifolds.begin(), m_cached_manifolds.end(), key, [](const CachedManifold& cached, CachedManifoldKey candidate) {
		    return cachedManifoldLess(cached, candidate);
	    }
	);
	if (manifold == m_cached_manifolds.end() || not cachedManifoldMatches(*manifold, key)) {
		return nullptr;
	}

	const size_t contact_count = std::min<size_t>(manifold->contact_count, manifold->contacts.size());
	const std::span<CachedContact> contacts(manifold->contacts.data(), contact_count);
	const auto contact = std::ranges::find_if(contacts, [feature_a, feature_b](const CachedContact& cached) {
		return cached.feature_a == feature_a && cached.feature_b == feature_b;
	});
	return contact != contacts.end() ? &*contact : nullptr;
}

auto Simulator::findCachedContact(
    const BroadPhasePair& pair, uint8_t normal_index, ContactFeatureID feature_a, ContactFeatureID feature_b
) const -> const CachedContact* {
	ZoneScopedN("physics::FindCachedContact");

	const CachedManifoldKey key {.pair = pair, .normal_index = normal_index};
	const auto manifold = std::lower_bound(
	    m_cached_manifolds.begin(), m_cached_manifolds.end(), key, [](const CachedManifold& cached, CachedManifoldKey candidate) {
		    return cachedManifoldLess(cached, candidate);
	    }
	);
	if (manifold == m_cached_manifolds.end() || not cachedManifoldMatches(*manifold, key)) {
		return nullptr;
	}

	const size_t contact_count = std::min<size_t>(manifold->contact_count, manifold->contacts.size());
	const std::span<const CachedContact> contacts(manifold->contacts.data(), contact_count);
	const auto contact = std::ranges::find_if(contacts, [feature_a, feature_b](const CachedContact& cached) {
		return cached.feature_a == feature_a && cached.feature_b == feature_b;
	});
	return contact != contacts.end() ? &*contact : nullptr;
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

	// A shared scratch array since islands never share a dynamic body so never reset between them
	std::vector<uint32_t> next_free_batch(m_bodies.size(), 0);

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

		// Greedy list coloring a constraint batch is one past the highest batch its dynamic bodies reached
		std::vector<uint32_t> constraint_batch(island.constraints.size());
		uint32_t batch_count = island.constraints.empty() ? 0 : 1;
		for (size_t i = 0; i < island.constraints.size(); ++i) {
			const Constraint& constraint = island.constraints[i];
			const Body* body_a = tryGetBody(constraint.body_a);
			const Body* body_b = tryGetBody(constraint.body_b);
			const bool a_dynamic = body_a != nullptr && body_a->inverse_mass > 0.0f;
			const bool b_dynamic = body_b != nullptr && body_b->inverse_mass > 0.0f;

			uint32_t batch = 0;
			if (a_dynamic) {
				batch = std::max(batch, next_free_batch[constraint.body_a.slot]);
			}
			if (b_dynamic) {
				batch = std::max(batch, next_free_batch[constraint.body_b.slot]);
			}
			constraint_batch[i] = batch;
			batch_count = std::max(batch_count, batch + 1);
			if (a_dynamic) {
				next_free_batch[constraint.body_a.slot] = batch + 1;
			}
			if (b_dynamic) {
				next_free_batch[constraint.body_b.slot] = batch + 1;
			}
		}

		// Counting sort into batch contiguous storage stable within a batch since i is scanned in order
		island.batch_offsets.assign(batch_count + 1, 0);
		for (uint32_t batch : constraint_batch) {
			++island.batch_offsets[batch + 1];
		}
		for (size_t i = 1; i < island.batch_offsets.size(); ++i) {
			island.batch_offsets[i] += island.batch_offsets[i - 1];
		}
		std::vector<Constraint> reordered(island.constraints.size());
		std::vector<size_t> cursor(island.batch_offsets.begin(), island.batch_offsets.end() - 1);
		for (size_t i = 0; i < island.constraints.size(); ++i) {
			reordered[cursor[constraint_batch[i]]++] = island.constraints[i];
		}
		island.constraints = std::move(reordered);
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

	const glm::vec3 r_a = contact.position - body_a->worldCenterOfMass();
	const glm::vec3 r_b = contact.position - body_b->worldCenterOfMass();
	const auto normal_mass = effectiveMassAlong(*body_a, *body_b, r_a, r_b, manifold.normal);
	if (not normal_mass) {
		return std::nullopt;
	}
	const glm::vec3 relative_velocity = velocityAtPoint(*body_b, r_b) - velocityAtPoint(*body_a, r_a);
	const float initial_normal_speed = glm::dot(relative_velocity, manifold.normal);
	const CachedContact* cached_contact =
	    findCachedContact(manifold.pair, manifold.normal_index, contact.feature_a, contact.feature_b);
	const float restitution = contact.material.restitution;
	const float bounce_threshold = tunables().bounce_threshold;
	float restitution_bias = 0.0f;
	if (initial_normal_speed < -bounce_threshold) {
		restitution_bias = -restitution * initial_normal_speed;
	}

	const glm::vec3 tangent_velocity = relative_velocity - manifold.normal * initial_normal_speed;
	const float tangent_length_sq = glm::dot(tangent_velocity, tangent_velocity);
	glm::vec3 tangent = {};
	float tangent_mass = 0.0f;

	if (tangent_length_sq > 1.0e-10f) {
		tangent = tangent_velocity / std::sqrt(tangent_length_sq);
	} else if (cached_contact) {
		const glm::vec3 projected_tangent =
		    cached_contact->tangent_impulse - manifold.normal * glm::dot(cached_contact->tangent_impulse, manifold.normal);
		const float projected_length_sq = glm::dot(projected_tangent, projected_tangent);
		if (projected_length_sq > 1.0e-10f && std::isfinite(projected_length_sq)) {
			tangent = projected_tangent / std::sqrt(projected_length_sq);
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

auto Simulator::solveConstraintBatch(std::span<Constraint> batch) -> size_t {
	size_t invalid_count = 0;
	for (Constraint& constraint : batch) {
		if (not solveConstraint(constraint)) {
			++invalid_count;
		}
	}
	return invalid_count;
}

void Simulator::solveIslands(std::vector<SimulationIsland>& islands) {
	ZoneScopedN("physics::SolveIslands");
	ZoneValue(static_cast<uint64_t>(islands.size()));

	if (islands.empty()) {
		return;
	}
	PhaseScope worker_phase {*this, SimulationPhase::mutation, SimulationPhase::worker_execution};

	const size_t worker_count = std::max(toast::ThreadPool::workerCount(), static_cast<size_t>(1));

	// Warm start is one pass over each island constraints cheap enough to just do right here
	for (SimulationIsland& island : islands) {
		warmStartConstraints(island.constraints);
	}

	// Solve in lockstep waves a wave can be chunked across islands since none share a dynamic body
	const uint32_t solver_iterations = tunables().solver_iterations;
	size_t max_batches = 0;
	for (const SimulationIsland& island : islands) {
		max_batches = std::max(max_batches, island.batch_offsets.empty() ? size_t {0} : island.batch_offsets.size() - 1);
	}
	m_profile.max_constraint_batches = max_batches;

	size_t invalid_constraint_count = 0;
	std::vector<std::span<Constraint>> wave_spans;
	for (uint32_t iteration = 0; iteration < solver_iterations; ++iteration) {
		ZoneScopedN("iteration");
		ZoneValue(static_cast<uint64_t>(iteration));

		for (size_t wave = 0; wave < max_batches; ++wave) {
			wave_spans.clear();
			for (SimulationIsland& island : islands) {
				if (wave + 1 >= island.batch_offsets.size()) {
					continue;
				}
				const size_t begin = island.batch_offsets[wave];
				const size_t end = island.batch_offsets[wave + 1];
				if (end > begin) {
					wave_spans.push_back(std::span<Constraint> {island.constraints}.subspan(begin, end - begin));
				}
			}
			if (wave_spans.empty()) {
				continue;
			}

			size_t wave_total = 0;
			for (const std::span<Constraint>& span : wave_spans) {
				wave_total += span.size();
			}
			if (wave_total < tunables().min_wave_constraints_for_dispatch) {
				// Not enough work this wave to be worth a thread pool round trip
				for (const std::span<Constraint>& span : wave_spans) {
					invalid_constraint_count += solveConstraintBatch(span);
				}
				continue;
			}

			auto chunks = chunkConstraintWave(wave_spans, worker_count);
			if (chunks.size() <= 1) {
				for (const std::span<Constraint>& span : wave_spans) {
					invalid_constraint_count += solveConstraintBatch(span);
				}
				continue;
			}

			std::vector<std::future<size_t>> futures;
			futures.reserve(chunks.size());
			for (ConstraintChunk& chunk : chunks) {
				futures.emplace_back(toast::ThreadPool::push([this, pieces = std::move(chunk.pieces)] {
					ZoneScopedN("physics::ConstraintWaveChunk");
					size_t invalid = 0;
					for (const std::span<Constraint>& piece : pieces) {
						invalid += solveConstraintBatch(piece);
					}
					return invalid;
				}));
			}
			for (auto& future : futures) {
				invalid_constraint_count += future.get();
			}
		}
	}

	if (invalid_constraint_count > 0) {
		TOAST_WARN("Physics", "Skipped {} invalid solver constraint(s)", invalid_constraint_count);
	}
	m_profile.invalid_constraints = invalid_constraint_count;

	// Position correction is one pass over each island manifolds same reasoning as warm start above
	size_t position_correction_count = 0;
	for (SimulationIsland& island : islands) {
		position_correction_count += correctPositions(island.manifold_indices);
	}
	m_profile.position_corrections = position_correction_count;

	m_profile.island_jobs = islands.size();

	for (const SimulationIsland& island : islands) {
		storeConstraintImpulses(island.constraints);
	}
}

auto Simulator::solveConstraint(Constraint& constraint) -> bool {
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
