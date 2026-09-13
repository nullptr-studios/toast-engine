/**
 * @file simulator.hpp
 * @author Xein
 * @date 09 Sep 2026
 * @brief This class simulates all of the physics of the project
 */

#pragma once

#include "body.hpp"
#include "collision.hpp"
#include "constraint.hpp"
#include "manifold.hpp"
#include "shape.hpp"
#include "physics_material.hpp"

#include <deque>
#include <optional>
#include <toast/export.hpp>
#include <toast/log.hpp>
#include <toast/world/box.hpp>
#include <toml++/impl/preprocessor.hpp>
#include <vector>

namespace physics {

class Rigidbody;

class TOAST_API Simulator {
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
	using ManifoldQueue = std::deque<Manifold>;

	struct NodeBinding {
		BodyID body;
		toast::Box<Rigidbody> node;
	};

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

	[[nodiscard]]
	auto broadPhase() const -> std::vector<BroadPhasePair>;
	[[nodiscard]]
	auto broadPhasePair(size_t shape_a, size_t shape_b) const -> std::optional<BroadPhasePair>;
	void clearManifoldQueues();
	void narrowPhase(const std::vector<BroadPhasePair>& candidates);
	void mergeManifoldQueues();
	void mergeManifold(const Manifold& manifold);
	void sortManifolds();
	[[nodiscard]]
	auto validateManifold(Manifold& manifold) const -> bool;

	[[nodiscard]]
	auto prepareConstraints(const std::vector<Manifold>& manifolds) const -> std::vector<Constraint>;
	[[nodiscard]]
	auto prepareConstraint(const Manifold& manifold, const ContactPoint& contact) const -> std::optional<Constraint>;
	void solveConstraints(std::vector<Constraint>& constraints);
	[[nodiscard]]
	auto solveConstraint(Constraint& constraint) -> bool;
	void publishTransforms();
	[[nodiscard]]
	auto publishTransform(NodeBinding& binding) -> bool;
	
	static auto velocityAtPoint(const Body& body, const glm::vec3& r) -> glm::vec3;
	static auto effectiveMassAlong(const Body& body_a, const Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& direction) -> std::optional<float>;
	static void applyImpulse(Body& body_a, Body& body_b, const glm::vec3& r_a, const glm::vec3& r_b, const glm::vec3& impulse);
	static auto solveNormal(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	static auto solveFriction(Constraint& constraint, Body& body_a, Body& body_b) -> bool;
	void correctPositions(const std::vector<Manifold>& manifolds);
	static void flipManifold(Manifold& manifold);
	
	void collide(BroadPhasePair pair);

	inline static Simulator* instance = nullptr;

	std::vector<BodySlot> m_bodies;
	std::deque<uint32_t> m_free_body_slots;
	std::vector<NodeBinding> m_node_bindings;

	std::vector<ShapeSlot> m_shapes;
	std::deque<uint32_t> m_free_shape_slots;

	glm::vec3 gravity = {0.0f, 0.0f, -9.8f};

	std::vector<ManifoldQueue> m_manifold_queues;
	std::vector<Manifold> m_manifolds;
};

}
