/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace physics {

class Rigidbody;
class ConvexCollider;

class PhysicsSystem {
public:
	static void start();
	static void stop();

	static auto gravity() -> glm::dvec2;

	static void AddRigidbody(Rigidbody* rb);
	static void RemoveRigidbody(Rigidbody* rb);
	static void AddCollider(ConvexCollider* c);
	static void RemoveCollider(ConvexCollider* c);

	PhysicsSystem() = default;
	~PhysicsSystem() = default;

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;
	PhysicsSystem(PhysicsSystem&&) = delete;
	PhysicsSystem& operator=(PhysicsSystem&&) = delete;

private:
	static auto get() noexcept -> std::optional<PhysicsSystem*>;
	static PhysicsSystem* instance;

	void Tick();

	void RigidbodyPhysics(Rigidbody* rb);

	struct M {
		std::chrono::duration<double> targetFrametime {1.0/50.0};
		unsigned char tickCount = 1;
		std::list<Rigidbody*> rigidbodies;
		std::list<ConvexCollider*> colliders;
		glm::dvec2 gravity = {0.0, -9.81};
	} m;

	// out of the struct to make sure this is ALWAYS the last
	std::jthread thread;
};

}
