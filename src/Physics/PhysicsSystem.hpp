/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 22 Dec 2025

#pragma once
#include <Engine/Physics/Line.hpp>

namespace physics {

class Rigidbody;
class Collider;

class PhysicsSystem {
public:
	static auto get() -> std::optional<PhysicsSystem*>;
	static auto create() -> std::optional<PhysicsSystem*>;

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;
	~PhysicsSystem();

	static void AddRigidbody(Rigidbody* rb);
	static void RemoveRigidbody(Rigidbody* rb);
	static void AddCollider(Collider* c);
	static void RemoveCollider(Collider* c);

private:
	PhysicsSystem() = default;
	static PhysicsSystem* m_instance;

	void Tick();
	void Wait() const;

	[[nodiscard]]
	auto GetColliderLines() -> std::vector<Line>;

	struct M {
		std::jthread physicsThread;
		const double frameTarget = 50.f;
		const unsigned collisionResolutionCount = 1;

		std::list<Rigidbody*> rigidbodies;
		std::list<Collider*> colliders;
	} m;

};

}
