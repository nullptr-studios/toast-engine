/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "Toast/Event/ListenerComponent.hpp"
#include "Toast/Physics/ColliderFlags.hpp"

#include <atomic>
#include <glm/glm.hpp>

namespace physics {
struct RayResult;
}

namespace physics {

class Rigidbody;
class BoxRigidbody;
class ConvexCollider;
class Trigger;
class Line;

class PhysicsSystem {
public:
	static void start();
	static void stop();

	static auto gravity() -> glm::dvec2;
	static auto pos_slop() -> double;
	static auto pos_ptc() -> double;
	static auto eps() -> double;
	static auto eps_small() -> double;

	/// @brief Call from render thread (Tick/LateTick) to update visual transforms with interpolation
	static void UpdateVisualInterpolation();

	/// @brief Get the fixed timestep in seconds (1/50 = 0.02)
	static auto GetFixedTimestep() -> double;

	static void AddRigidbody(Rigidbody* rb);
	static void RemoveRigidbody(Rigidbody* rb);
	static void AddCollider(ConvexCollider* c);
	static void RemoveCollider(ConvexCollider* c);
	static void AddTrigger(Trigger* t);
	static void RemoveTrigger(Trigger* t);
	static void AddBox(BoxRigidbody* rb);
	static void RemoveBox(BoxRigidbody* rb);
	static std::optional<RayResult> RayCollision(Line* ray, ColliderFlags flags);

	PhysicsSystem();
	~PhysicsSystem();

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;
	PhysicsSystem(PhysicsSystem&&) = delete;
	PhysicsSystem& operator=(PhysicsSystem&&) = delete;

private:
	static auto get() noexcept -> std::optional<PhysicsSystem*>;
	static PhysicsSystem* instance;

	void Tick();

	void RigidbodyPhysics(Rigidbody* rb);
	void BoxPhysics(BoxRigidbody* rb);

	struct M {
		std::chrono::duration<double> targetFrametime { 1.0 / 50.0 };
		unsigned char tickCount = 1;
		std::list<Rigidbody*> rigidbodies;
		std::list<BoxRigidbody*> boxes;
		std::list<ConvexCollider*> colliders;
		std::list<Trigger*> triggers;
		glm::dvec2 gravity = { 0.0, -9.81 };
		double positionCorrectionSlop = 1.0e-3;
		double positionCorrectionPtc = 0.4;
		double eps = 1.0e-6;
		double epsSmall = 1.0e-9;

		// Interpolation thingi
		std::atomic<std::chrono::steady_clock::time_point> lastPhysicsTime { std::chrono::steady_clock::now() };

		event::ListenerComponent eventListener;
	} m;

	// out of the struct to make sure this is ALWAYS the last
	std::jthread thread;
};

}
