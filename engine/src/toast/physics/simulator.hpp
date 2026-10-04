/**
 * @file simulator.hpp
 * @author Xein
 * @date 09 Sep 2026
 * @brief This class simulates all of the physics of the project
 */

#pragma once

#include "body.hpp"
#include "broad_phase.hpp"
#include "constraint.hpp"
#include "damage_command.hpp"
#include "fragment_extraction.hpp"
#include "manifold.hpp"
#include "narrow_phase.hpp"
#include "physics_material.hpp"
#include "shape.hpp"
#include "shape_query.hpp"
#include "toast/physics/raycast.hpp"
#include "voxel_render.hpp"
#include "voxel_shape_data.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <thread>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/voxel/connectivity.hpp>
#include <toast/voxel/nodes/voxel_node.hpp>
#include <toast/world/box.hpp>
#include <toml++/impl/preprocessor.hpp>
#include <unordered_map>
#include <vector>

namespace physics {
class Trigger;

class Rigidbody;
class DynamicRigidbody;
class Collider;
struct SmashVolume;

}

namespace physics {

class TOAST_API Simulator {
	friend class Collider;
	friend class Rigidbody;
	friend class DynamicRigidbody;
	friend class toast::VoxelNode;

public:
	struct DebugDirtyBrick {
		ShapeID shape;
		glm::ivec3 brick {};
	};

	struct ConnectivityResult {
		ShapeID shape;
		voxel::Connectivity connectivity;
	};

	Simulator();
	~Simulator();
	Simulator(const Simulator&) = delete;
	auto operator=(const Simulator&) -> Simulator& = delete;
	Simulator(Simulator&&) = delete;
	auto operator=(Simulator&&) -> Simulator& = delete;

	void tick();
	void integrate(float dt);

	[[nodiscard]]
	auto createBody(const BodyDescriptor& descriptor) -> BodyID;
	void destroyBody(BodyID body);

	static auto raycast(glm::vec3 pos, glm::vec3 dir, float max_distance = -1.0f, int max_targets = 3) -> std::vector<RayHit>;

	[[nodiscard]]
	auto valid(BodyID body) const -> bool;
	[[nodiscard]]
	auto state(BodyID body) const -> std::optional<BodyState>;

	auto setTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation) -> bool;
	auto setLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool;

	void recordDamage(DamageCommand&& command);
	void applyDamageCommands();
	void applyDamageCommand(const DamageCommand& c);
	void applyExplosion(const glm::vec3& position, float radius, float energy);

	auto shootVoxel(
	    const glm::vec3& origin, const glm::vec3& direction, float max_distance, float energy, float min_radius = 0.0f,
	    const std::optional<FragmentPush>& push = std::nullopt
	) -> bool;

	auto smashCapsule(
	    const CapsuleShape& capsule, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& sweep,
	    const CapsuleSmash& smash
	) -> uint32_t;

	/// Burns a thin tube into the first voxels along the ray right away, the radius ignores the material shatter radius
	auto cutVoxel(const glm::vec3& origin, const glm::vec3& direction, float max_distance, float energy, float radius, float depth)
	    -> uint32_t;

	[[nodiscard]]
	auto runConnectivityAnalysis() -> std::vector<ConnectivityResult>;

	[[nodiscard]]
	auto debugDirtyBricks() const -> std::span<const DebugDirtyBrick> {
		return m_debug_dirty_bricks;
	}

	/// Counters and phase timings for one tick snapshotted at a safe point for a debug view to read
	struct PhysicsStepProfile {
		std::array<size_t, static_cast<size_t>(NarrowPhasePairType::count)> narrow_pair_candidates = {};
		std::array<double, static_cast<size_t>(NarrowPhasePairType::count)> narrow_pair_time_ms = {};
		size_t narrow_jobs = 0;
		size_t narrow_candidates = 0;
		/// Of narrow_pair_candidates box_voxel/voxel_voxel how many crossed the split threshold
		size_t box_voxel_split_pairs = 0;
		size_t voxel_voxel_split_pairs = 0;
		/// Of resolved voxel-voxel candidates how many had a non-degenerate overlap
		size_t voxel_voxel_resolved_pairs = 0;
		size_t voxel_voxel_nondegenerate_pairs = 0;
		/// Largest estimated_voxels seen this tick regardless of threshold
		size_t box_voxel_max_estimated_voxels = 0;
		size_t voxel_voxel_max_estimated_voxels = 0;
		/// World-space longest axis metres behind box_voxel_max_estimated_voxels
		float box_voxel_max_extent_meters = 0.0f;
		size_t narrow_collisions = 0;
		size_t sleeping_pairs_skipped = 0;
		size_t rejected_manifolds = 0;
		size_t contact_points = 0;
		size_t bodies_woken = 0;
		size_t bodies_slept = 0;
		/// Of bodies_woken what woke each sleeper
		size_t woken_by_approach = 0;
		size_t woken_by_racing = 0;
		size_t woken_by_contact_end = 0;
		size_t woken_by_support_loss = 0;
		size_t contact_begins = 0;
		size_t contact_persists = 0;
		size_t contact_ends = 0;
		size_t reused_cached_contacts = 0;
		size_t cold_cached_contacts = 0;
		size_t constraints = 0;
		size_t rejected_constraints = 0;
		size_t warm_started_constraints = 0;
		size_t island_jobs = 0;
		size_t invalid_constraints = 0;
		size_t position_corrections = 0;

		double tick_ms = 0.0;
		double damage_apply_ms = 0.0;
		double connectivity_ms = 0.0;
		double character_step_ms = 0.0;
		double narrow_phase_ms = 0.0;
		double solve_ms = 0.0;

		/// Call counts this tick not character count a movement tick calls trace/fits several times
		size_t character_sweep_calls = 0;
		size_t character_overlap_calls = 0;

		/// solve_ms broken down island_solve_ms is solveIslands itself the rest is bookkeeping around it
		double cache_update_ms = 0.0;
		double wake_groups_ms = 0.0;
		double prepare_constraints_ms = 0.0;
		double build_islands_ms = 0.0;
		double island_solve_ms = 0.0;
		double sleep_update_ms = 0.0;

		size_t body_count = 0;
		size_t awake_body_count = 0;
		size_t voxel_shape_count = 0;
		size_t manifold_count = 0;

		size_t damage_commands = 0;
		size_t dirty_bricks = 0;
		size_t surface_bricks_repaired = 0;

		size_t connectivity_jobs = 0;
		size_t connectivity_jobs_dispatched = 0;
		size_t connectivity_jobs_stale = 0;
		size_t connectivity_shapes_waiting = 0;

		size_t fragments_spawned = 0;
		size_t fragment_spawn_failures = 0;
		size_t fragments_pending = 0;
		size_t fragments_active = 0;
		size_t fragments_sleep_locked = 0;
		size_t fragments_despawned = 0;
		size_t fragments_evicted = 0;
		size_t static_splits_spawned = 0;

		uint32_t brick_pool_allocated = 0;
		uint32_t brick_pool_capacity = 0;

		/// Peak constraint batch count across islands this tick
		size_t max_constraint_batches = 0;

		/// How many ticks the fixed step accumulator ran this frame since tick_ms above is only the last one
		size_t ticks_this_frame = 1;

		/// The accumulator cut this frame catch up burst short on time rather than running out of work
		bool ticks_capped_by_time_budget = false;
	};

	/// The last tick profile is safe to read from another thread between ticks
	[[nodiscard]]
	static auto stepProfile() -> const PhysicsStepProfile&;

	/// Stamps how many ticks the accumulator ran once it stops catching up for this frame
	static void recordTickBurst(size_t steps, bool time_budget_reached);

	/// Leftover accumulator time as a fraction of one step for blending the last two poses
	static void recordInterpolationAlpha(double alpha);
	[[nodiscard]]
	static auto interpolationAlpha() -> float;

	/// World transform from the tick pose to the blended pose drawn this frame and identity unless dynamic and awake
	[[nodiscard]]
	static auto renderPoseDelta(BodyID id) -> glm::mat4;
	[[nodiscard]]
	static auto renderPoseDelta(const toast::VoxelNode& node) -> glm::mat4;

	/// Delta of the dynamic body a node sits below and identity when it follows none
	[[nodiscard]]
	static auto renderPoseDeltaFor(uint64_t node_uid) -> glm::mat4;

	/// Once per frame after the ticks so nodes below driven bodies match the tick pose the delta starts from
	static void syncRenderPoses();

	[[nodiscard]]
	static auto shapeWorldBounds(ShapeID shape) -> std::optional<AABB>;

	/// Rigidbody or VoxelNode that owns @c body
	[[nodiscard]]
	static auto nodeFor(BodyID body) -> toast::Box<toast::Node>;

	static void callTick();
	static void registerRigidbody(Rigidbody& node);
	static void unregisterRigidbody(Rigidbody& node);
	static void registerVoxelNode(toast::VoxelNode& node);
	static void unregisterVoxelNode(toast::VoxelNode& node);
	static void registerTrigger(Trigger& node);
	static void unregisterTrigger(Trigger& node);

	[[nodiscard]]
	auto overlapCapsule(
	    BodyID ignored, const CapsuleShape& capsule, const glm::vec3& position, const glm::quat& rotation, float min_penetration,
	    std::vector<QueryContact>& contacts
	) const -> bool;
	/// Tests a world-space sphere against enabled physics shapes.
	[[nodiscard]]
	auto overlapSphere(
	    const SphereShape& sphere, const glm::vec3& position, float min_penetration, std::vector<QueryContact>& contacts
	) const -> bool;
	/// Tests a world-space axis-aligned box against enabled physics shapes.
	[[nodiscard]]
	auto overlapAABB(const AABB& bounds) const -> bool;
	/// Moves a capsule until it would dig into something
	[[nodiscard]]
	auto sweepCapsule(
	    BodyID ignored, const CapsuleShape& capsule, const glm::quat& rotation, const glm::vec3& from, const glm::vec3& to,
	    float skin
	) const -> SweepHit;
	/// Resizes the capsule collider of a body
	auto setCapsuleShape(BodyID body, const CapsuleShape& capsule) -> bool;
	void moveKinematicBody(BodyID id, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& velocity);
	/// Raises the velocity of a dynamic body along direction
	void pushBody(BodyID id, const glm::vec3& point, const glm::vec3& direction, float speed, float max_impulse);

	/// Simulator running the current step, null outside a play session
	[[nodiscard]]
	static auto current() -> Simulator*;

	[[nodiscard]]
	static auto voxelFragmentRecords() -> std::span<const VoxelRenderRecord>;

private:
	enum class SimulationPhase : uint8_t {
		idle,
		mutation,
		worker_execution,
	};

	class PhaseScope {
	public:
		PhaseScope(Simulator& simulator, SimulationPhase expected, SimulationPhase next);
		~PhaseScope();
		PhaseScope(const PhaseScope&) = delete;
		auto operator=(const PhaseScope&) -> PhaseScope& = delete;

	private:
		Simulator& m_simulator;
		SimulationPhase m_previous;
		SimulationPhase m_active;
	};

	struct ColliderBinding {
		ShapeID shape;
		toast::Box<Collider> node;
	};

	struct NodeBinding {
		BodyID body;
		toast::Box<Rigidbody> node;
		std::vector<ColliderBinding> colliders;
	};

	struct VoxelNodeBinding {
		BodyID body;
		ShapeID shape;
		toast::Box<toast::VoxelNode> node;
		uint32_t source_revision = 0;
		uint64_t source_model = 0;
		uint64_t source_palette = 0;
	};

	struct SimulationIsland {
		BodyID sort_key;
		std::vector<BodyID> dynamic_bodies;
		std::vector<Constraint> constraints;
		std::vector<size_t> manifold_indices;

		/// constraints[batch_offsets[i] .. batch_offsets[i + 1]) only share a dynamic body within one body pair run
		/// and each body pair run is contiguous, so a batch can be split anywhere except inside a run
		std::vector<size_t> batch_offsets;
	};

	struct FragmentRecord {
		BodyID body;
		ShapeID shape;
		uint64_t sequence = 0;
		float rest_seconds = 0.0f;
		glm::vec3 rest_center = {};
		glm::quat rest_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	};

	struct PendingFragments {
		ShapeID shape;
		std::vector<DetachedComponent> components;
		size_t cursor = 0;
		std::optional<FragmentPush> push;
	};

	struct VoxelRayHit {
		ShapeID shape;
		float distance = 0.0f;
		float shatter_radius = 0.0f;
	};

	[[nodiscard]]
	static auto colliderFor(BodyID body, ShapeID shape) -> toast::Box<toast::Node>;
	[[nodiscard]]
	auto mainThreadMutationAllowed() const -> bool;

	[[nodiscard]]
	auto createSphere(BodyID owner, const SphereShape& sphere, PhysicsMaterial material) -> ShapeID;
	[[nodiscard]]
	auto createBox(BodyID owner, const BoxShape& box, PhysicsMaterial material) -> ShapeID;
	[[nodiscard]]
	auto createCapsule(BodyID owner, const CapsuleShape& capsule, PhysicsMaterial material) -> ShapeID;
	[[nodiscard]]
	auto createVoxelShape(
	    BodyID owner, const VoxelShape& shape, voxel::Volume& volume, const voxel::Palette& palette,
	    const voxel::MaterialLibrary& materials
	) -> ShapeID;
	[[nodiscard]]
	auto createVoxelShape(
	    BodyID owner, const VoxelShape& shape, voxel::Volume&& volume, const voxel::Palette& palette,
	    const voxel::MaterialLibrary& materials
	) -> ShapeID;
	[[nodiscard]]
	auto createVoxelShape(BodyID owner, toast::VoxelNode& node) -> ShapeID;

	void destroyShape(ShapeID shape);
	[[nodiscard]]
	auto valid(ShapeID shape) const -> bool;
	[[nodiscard]]
	auto tryGetShape(ShapeID shape) -> Shape*;
	[[nodiscard]]
	auto tryGetShape(ShapeID shape) const -> const Shape*;
	[[nodiscard]]
	auto valid(VoxelDataID data) const -> bool;

public:
	[[nodiscard]]
	static auto tryGetVoxelData(VoxelDataID data) -> VoxelShapeData*;

private:
	void destroyVoxelData(VoxelDataID data);

	void rebuildMassProperties(BodyID id);

	[[nodiscard]]
	auto tryGetBody(BodyID body) -> Body*;
	[[nodiscard]]
	auto tryGetBody(BodyID body) const -> const Body*;

	static void integrateBody(BodyID id, Body& body, const glm::vec3& gravity, float dt);
	static void setBodyEnabled(BodyID body, bool enabled);

	static void setBodyTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation);
	static auto setBodyLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool;

	static void setShapeEnabled(ShapeID shape, bool enabled);
	void syncEnabledState();

	[[nodiscard]]
	auto queryCandidates(BodyID ignored, const AABB& bounds) const -> std::vector<ShapeID>;
	void collideCapsuleProbe(
	    const Body& probe_body, const Shape& probe_shape, std::span<const ShapeID> candidates, std::vector<QueryContact>& contacts
	) const;
	void collideSphereProbe(
	    const Body& probe_body, const Shape& probe_shape, std::span<const ShapeID> candidates, std::vector<QueryContact>& contacts
	) const;

	static void wakeBody(BodyID id);
	static void sleepBody(BodyID id);
	/// A lost contact wakes a sleeper only when the body leaving is moving or nothing else still holds it up
	void releaseSleeper(BodyID sleeper, BodyID other, const glm::vec3& normal_to_other);
	void wakeBodiesTouching(BodyID id);
	void wakeBodiesTouching(ShapeID id);
	void wakeBodiesInBounds(const AABB& bounds);
	void convertImpulsesToDamage(std::span<const SimulationIsland> islands);
	void wakeDisturbedSleepers(float dt);
	void unlockDisturbedFragments();
	void markSupportedBodies();
	[[nodiscard]]
	auto isStill(const Body& body, float scale) const -> bool;
	[[nodiscard]]
	auto sleepScale(size_t slot) const -> float;
	[[nodiscard]]
	auto sleeperIsHeldUp(BodyID id) const -> bool;
	void updateSleeping(float dt);
	[[nodiscard]]
	auto shouldSolve(const Manifold& manifold) const -> bool;
	[[nodiscard]]
	auto pairNeedsNarrowPhase(CollisionWorldView world, const BroadPhasePair& pair) const -> bool;

	[[nodiscard]]
	auto generateManifoldsAsync(CollisionWorldView world, std::span<const BroadPhasePair> candidates) -> std::vector<Manifold>;

	[[nodiscard]]
	auto prepareConstraints(const std::vector<Manifold>& manifolds) -> std::vector<Constraint>;
	[[nodiscard]]
	auto prepareConstraint(const Manifold& manifold, const ContactPoint& contact) const -> std::optional<Constraint>;
	[[nodiscard]]
	auto buildIslands(std::span<const Manifold> manifolds, const std::vector<Constraint>& constraints)
	    -> std::vector<SimulationIsland>;
	void updateCache(std::span<const Manifold> manifolds);
	[[nodiscard]]
	auto findCachedContact(const BroadPhasePair& pair, uint8_t normal_index, ContactFeatureID feature_a, ContactFeatureID feature_b)
	    -> CachedContact*;
	[[nodiscard]]
	auto findCachedContact(
	    const BroadPhasePair& pair, uint8_t normal_index, ContactFeatureID feature_a, ContactFeatureID feature_b
	) const -> const CachedContact*;
	void warmStartConstraints(std::span<Constraint> constraints);
	void storeConstraintImpulses(std::span<const Constraint> constraints);
	[[nodiscard]]
	auto shapeRevision(ShapeID shape) const -> uint32_t;
	void incrementShapeRevision(ShapeID shape);
	void solveIslands(std::vector<SimulationIsland>& islands);
	[[nodiscard]]
	auto solveConstraintBatch(std::span<Constraint> batch) -> size_t;
	[[nodiscard]]
	auto solveConstraint(Constraint& constraint) -> bool;
	void publishTransforms();
	[[nodiscard]]
	auto publishTransform(NodeBinding& binding) -> bool;
	[[nodiscard]]
	auto publishVoxelTransform(VoxelNodeBinding& binding) -> bool;
	void publishVoxelRenderRecords();
	void rebuildRenderPoseLookup();

	static auto velocityAtPoint(const Body& body, const glm::vec3& r) -> glm::vec3;
	static auto effectiveMassAlong(
	    const Body& body_a, const Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction
	) -> std::optional<float>;
	static void applyImpulse(Body& body_a, Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse);
	static auto solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	static auto solveFriction(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	[[nodiscard]]
	auto correctPositions(std::span<const size_t> manifold_indices) -> size_t;
	void publishProfile(std::span<const SimulationIsland> islands);

	void clearFragmentFromSource(
	    ShapeID shape_id, VoxelShapeData& data, voxel::Volume& source, const DetachedComponent& component,
	    const RemovedVoxels* already_removed = nullptr
	);
	[[nodiscard]]
	auto spawnFragmentBody(ShapeID source_shape_id, const DetachedComponent& component, const FragmentPush* push = nullptr) -> bool;
	auto smashShape(ShapeID shape_id, const SmashVolume& volume, const CapsuleSmash& smash, bool chunks) -> uint32_t;
	[[nodiscard]]
	auto marchVoxelRay(const glm::vec3& origin, const glm::vec3& direction, float max_distance) -> std::optional<VoxelRayHit>;
	[[nodiscard]]
	auto spawnStaticSplitBody(ShapeID source_shape_id, const DetachedComponent& component) -> bool;
	void despawnSettledFragments(float dt);
	void retireVoxelBody(BodyID id);
	void destroyFragmentsOf(BodyID origin);

	[[nodiscard]]
	auto voxelNodeFor(ShapeID shape) -> toast::VoxelNode*;

	void reapFragments();
	void destroyFragmentRecord(BodyID id);
	void queuePendingFragments(std::span<const ConnectivityResult> results);
	void spawnBudgetedFragments();
	[[nodiscard]]
	auto reconcileComponent(const voxel::Volume& volume, const DetachedComponent& component) const -> bool;
	void enforceFragmentBudget(float dt);
	void unlockSleep(BodyID id);
	void recheckNeighborsOf(BodyID id);
	void rebuildFragmentIndex();
	void refreshPalette(uint64_t palette_uid);
	auto createVoxelShapeInternal(
	    BodyID owner, const VoxelShape& shape, voxel::Volume* external, std::unique_ptr<voxel::Volume>& owned,
	    const voxel::Palette& palette, const voxel::MaterialLibrary& materials, const voxel::MassMoments* known_moments = nullptr
	) -> ShapeID;

	void checkTriggers();
	void overlapShape(
	    BodyID ignored, const Shape& probe, const glm::vec3& position, const glm::quat& rotation, std::vector<BodyID>& bodies
	) const;

	inline static Simulator* instance = nullptr;

	mutable std::thread::id m_owner_thread;
	std::atomic<SimulationPhase> m_phase = SimulationPhase::idle;

	std::vector<BodySlot> m_bodies;
	std::deque<uint32_t> m_free_body_slots;
	std::vector<NodeBinding> m_node_bindings;
	std::vector<VoxelNodeBinding> m_voxel_bindings;

	/// Persistent wake/sleep scratch sized to m_bodies each tick instead of reallocated
	std::vector<uint8_t> m_body_supported;
	std::vector<uint8_t> m_sleep_ready;
	std::vector<uint8_t> m_sleep_moving;
	std::vector<uint8_t> m_sleep_blocked;
	/// A cached contact with something below holds the body up
	std::vector<uint8_t> m_contact_supported;
	/// Overlap already removed from a body this pass so stacked manifolds stop pushing the same overlap twice
	std::vector<glm::vec3> m_position_shift;

	/// Persistent buildIslands scratch reused across ticks and across islands instead of reallocated
	std::vector<size_t> m_island_parents;
	std::vector<bool> m_island_participates;
	std::vector<size_t> m_island_by_root;
	std::vector<uint32_t> m_island_next_free_batch;
	std::vector<uint32_t> m_island_constraint_batch;
	std::vector<size_t> m_island_batch_cursor;
	std::vector<Constraint> m_island_reordered_scratch;

	/// Reused across waves instead of a fresh vector every iteration x wave x tick
	std::vector<size_t> m_wave_chunk_invalid_counts;

	std::vector<ShapeSlot> m_shapes;
	std::deque<uint32_t> m_free_shape_slots;

	BroadPhase m_broad_phase;
	NarrowPhase m_narrow_phase;
	std::vector<Manifold> m_manifolds;
	std::vector<CachedManifold> m_cached_manifolds;
	/// mutable so const query methods (overlapCapsule/sweepCapsule) can still count how often they run
	mutable PhysicsStepProfile m_profile;
	PhysicsStepProfile m_published_profile;

	std::vector<VoxelShapeSlot> m_voxel_shapes;
	std::deque<uint32_t> m_free_voxel_shape_slots;
	std::vector<VoxelRenderRecord> m_voxel_render_records;

	std::mutex m_damage_mutex;
	std::vector<DamageCommand> m_damage_commands;
	std::vector<DebugDirtyBrick> m_debug_dirty_bricks;

	uint64_t m_next_fragment_sequence = 1;
	std::vector<FragmentRecord> m_fragments;
	/// body slot -> index in m_fragments rebuilt whenever an erase shifts indices
	std::unordered_map<uint32_t, size_t> m_fragment_index;
	std::vector<PendingFragments> m_pending_fragments;
	std::vector<BodyID> m_doomed_fragments;

	/// Round robin start so a connectivity job cap does not starve the same shapes
	size_t m_connectivity_cursor = 0;

	float m_interpolation_alpha = 0.0f;

	/// Nearest dynamic body above each node uid rebuilt by syncRenderPoses
	std::unordered_map<uint64_t, BodyID> m_render_body_of_node;

	event::Listener m_listener;

	// triggers stuff
	std::vector<toast::Box<Trigger>> m_triggers;
};

}
