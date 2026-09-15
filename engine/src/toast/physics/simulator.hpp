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
#include "manifold.hpp"
#include "narrow_phase.hpp"
#include "physics_material.hpp"
#include "shape.hpp"

#include <deque>
#include <optional>
#include <span>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/world/box.hpp>
#include <toml++/impl/preprocessor.hpp>
#include <vector>

namespace physics {

class Rigidbody;
class DynamicRigidbody;
class Collider;

class TOAST_API Simulator {
	friend class Collider;
	friend class Rigidbody;
	friend class DynamicRigidbody;

public:
	Simulator();
	~Simulator();

	void tick();
	void integrate(float dt);

	[[nodiscard]]
	auto createBody(const BodyDescriptor& descriptor) -> BodyID;
	void destroyBody(BodyID body);

	[[nodiscard]]
	auto valid(BodyID body) const -> bool;
	[[nodiscard]]
	auto state(BodyID body) const -> std::optional<BodyState>;

	auto setTransform(BodyID body, const glm::vec3& position, const glm::quat& rotation) -> bool;
	auto setLinearVelocity(BodyID body, const glm::vec3& velocity) -> bool;

	static void callTick();
	static void registerRigidbody(Rigidbody& node);
	static void unregisterRigidbody(Rigidbody& node);

private:
	struct ColliderBinding {
		ShapeID shape;
		toast::Box<Collider> node;
	};

	struct NodeBinding {
		BodyID body;
		toast::Box<Rigidbody> node;
		std::vector<ColliderBinding> colliders;
	};

	struct SimulationIsland {
		BodyID sort_key;
		std::vector<BodyID> dynamic_bodies;
		std::vector<Constraint> constraints;
		std::vector<size_t> manifold_indices;
	};

	struct PhysicsStepProfile {
		std::array<size_t, static_cast<size_t>(NarrowPhasePairType::count)> narrow_pair_candidates = {};
		size_t narrow_jobs = 0;
		size_t narrow_candidates = 0;
		size_t narrow_collisions = 0;
		size_t rejected_manifolds = 0;
		size_t contact_points = 0;
		size_t bodies_woken = 0;
		size_t bodies_slept = 0;
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
	};

	struct IslandSolveStats {
		size_t invalid_constraints = 0;
		size_t position_corrections = 0;
	};

	[[nodiscard]]
	static auto rigidbodyFor(BodyID body) -> toast::Box<Rigidbody>;

	[[nodiscard]]
	auto createSphere(BodyID owner, const SphereShape& sphere, PhysicsMaterial material) -> ShapeID;
	[[nodiscard]]
	auto createBox(BodyID owner, const BoxShape& box, PhysicsMaterial material) -> ShapeID;
	[[nodiscard]]
	auto createCapsule(BodyID owner, const CapsuleShape& capsule, PhysicsMaterial material) -> ShapeID;

	void destroyShape(ShapeID shape);
	[[nodiscard]]
	auto valid(ShapeID shape) const -> bool;
	[[nodiscard]]
	auto tryGetShape(ShapeID shape) -> Shape*;
	[[nodiscard]]
	auto tryGetShape(ShapeID shape) const -> const Shape*;

	void rebuildMassProperties(BodyID id);

	[[nodiscard]]
	auto tryGetBody(BodyID body) -> Body*;
	[[nodiscard]]
	auto tryGetBody(BodyID body) const -> const Body*;

	static void integrateBody(BodyID id, Body& body, const glm::vec3& gravity, float dt);
	static void setBodyEnabled(BodyID body, bool enabled);
	static void setShapeEnabled(ShapeID shape, bool enabled);
	void syncEnabledState();
	static void wakeBody(BodyID id);
	static void sleepBody(BodyID id);
	void wakeBodiesTouching(BodyID id);
	void wakeContactGroups();
	void updateSleeping(float dt);
	[[nodiscard]]
	auto shouldSolve(const Manifold& manifold) const -> bool;

	[[nodiscard]]
	auto generateManifoldsAsync(CollisionWorldView world, std::span<const BroadPhasePair> candidates) -> std::vector<Manifold>;

	[[nodiscard]]
	auto prepareConstraints(const std::vector<Manifold>& manifolds) -> std::vector<Constraint>;
	[[nodiscard]]
	auto prepareConstraint(const Manifold& manifold, const ContactPoint& contact) const -> std::optional<Constraint>;
	[[nodiscard]]
	auto buildIslands(std::span<const Manifold> manifolds, std::vector<Constraint> constraints) const
	    -> std::vector<SimulationIsland>;
	void updateCache(std::span<const Manifold> manifolds);
	[[nodiscard]]
	auto findCachedContact(const BroadPhasePair& pair, ContactFeatureID feature_a, ContactFeatureID feature_b) -> CachedContact*;
	[[nodiscard]]
	auto findCachedContact(const BroadPhasePair& pair, ContactFeatureID feature_a, ContactFeatureID feature_b) const
	    -> const CachedContact*;
	void warmStartConstraints(std::span<Constraint> constraints);
	void storeConstraintImpulses(std::span<const Constraint> constraints);
	[[nodiscard]]
	auto shapeRevision(ShapeID shape) const -> uint32_t;
	void incrementShapeRevision(ShapeID shape);
	void solveIslands(std::vector<SimulationIsland>& islands);
	[[nodiscard]]
	auto solveIsland(SimulationIsland& island) -> IslandSolveStats;
	[[nodiscard]]
	auto solveConstraint(Constraint& constraint) -> bool;
	void publishTransforms();
	[[nodiscard]]
	auto publishTransform(NodeBinding& binding) -> bool;

	static auto velocityAtPoint(const Body& body, const glm::vec3& r) -> glm::vec3;
	static auto effectiveMassAlong(
	    const Body& body_a, const Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction
	) -> std::optional<float>;
	static void applyImpulse(Body& body_a, Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse);
	static auto solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	static auto solveFriction(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	[[nodiscard]]
	auto correctPositions(std::span<const size_t> manifold_indices) -> size_t;
	void publishProfile(std::span<const SimulationIsland> islands) const;

	inline static Simulator* instance = nullptr;

	std::vector<BodySlot> m_bodies;
	std::deque<uint32_t> m_free_body_slots;
	std::vector<NodeBinding> m_node_bindings;

	std::vector<ShapeSlot> m_shapes;
	std::deque<uint32_t> m_free_shape_slots;

	glm::vec3 gravity = {0.0f, 0.0f, -9.8f};

	BroadPhase m_broad_phase;
	NarrowPhase m_narrow_phase;
	std::vector<Manifold> m_manifolds;
	std::vector<CachedManifold> m_cached_manifolds;
	PhysicsStepProfile m_profile;
};

}
