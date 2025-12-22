/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 22 Dec 2025

#pragma once

#include <Engine/Core/ThreadPool.hpp>
namespace physics {

class Rigidbody;

class PhysicsSystem {
public:
	static auto get() -> std::optional<PhysicsSystem*>;
	static auto create() -> std::optional<std::unique_ptr<PhysicsSystem>>;

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;

	static void AddRigidbody(Rigidbody* rb);

private:
	static PhysicsSystem* m_instance;
	struct Key { explicit Key() = default; };

	void Tick();
	void Wait() const;

	struct M {
		toast::ThreadPool threadPool;
		const double frameTarget = 50.f;
		const unsigned collisionResolutionCount = 1;

		std::list<Rigidbody*> rigidbodies;
	} m;

public:
	PhysicsSystem(Key);

};

}
