/**
 * @file PhysicsSystem.hpp
 * @author Iñaki
 * @date 06/10/2025
 *
 * @brief Singleton that handles the physics for the game
 */
#pragma once
#include "Engine/Event/ListenerComponent.hpp"
#include "Engine/Physics/Colliders/Collider.hpp"
#include "Engine/Physics/RigidbodyComponent.hpp"

namespace physics {

/// @brief singleton that handles the physics for the game
class PhysicsSystem {
public:
	/// @brief creates the system if it does not exist and sets the thread
	PhysicsSystem();

	static PhysicsSystem* GetInstance();

	/// @brief Called every time the physics is updated
	void Tick();

	/// @brief Called when the physics engine is being deleted
	void Destroy();

	/// @brief getter for a list of all rigidbodies
	[[nodiscard]]
	const std::deque<RigidbodyComponent*>& GetRigidBodies() const;

	/// @brief  adds a rigidbody to the list
	void AddRigidbody(RigidbodyComponent* rigidbody);

	/// @brief  removes a rigidbody of the list
	void RemoveRigidbody(const RigidbodyComponent* rigidbody);

	/// @brief getter for a list of all colliders
	[[nodiscard]]
	const std::deque<ICollider*>& GetColliders() const;

	/// @brief  adds a collider to the list
	void AddCollider(ICollider* collider);

	/// @brief  removes a colliders of the list
	void RemoveCollider(const ICollider* collider);

	[[nodiscard]]
	static glm::vec2 gravity();
	static void gravity(glm::vec2 multiplier);

	/// @brief Change the tick rate of the physics
	static void SetTickRate(float value);

	/// @brief Asks the physics system to halt simulation
	static void RequestHaltSimulation();

	/// @brief Returns false while waiting for the physics to halt simulation
	static bool WaitForAnswer();

	/// @brief Tells the physics system to resume simulation
	static void ReceivedAnswer();

private:
	static PhysicsSystem* m_instance;
	event::ListenerComponent* m_listener = nullptr;

	glm::vec2 m_gravity = { 0.0f, -9.67f };
	double m_targetDelta = 1.0 / 50.0;
	std::deque<RigidbodyComponent*> m_rigidbodies;
	std::deque<ICollider*> m_colliders;

	std::atomic_bool m_simulateWorld = true;

	std::atomic_bool m_requestReceived = false;
	std::atomic_bool m_requestAnswered = false;
};

// Wrappers for the Add/Remove functions
inline void AddRigidbody(RigidbodyComponent* rigidbody) {
	PhysicsSystem::GetInstance()->AddRigidbody(rigidbody);
}

inline void RemoveRigidbody(const RigidbodyComponent* rigidbody) {
	PhysicsSystem::GetInstance()->RemoveRigidbody(rigidbody);
}

inline void AddCollider(ICollider* collider) {
	PhysicsSystem::GetInstance()->AddCollider(collider);
}

inline void RemoveCollider(const ICollider* collider) {
	PhysicsSystem::GetInstance()->RemoveCollider(collider);
}

}
