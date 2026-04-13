/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "Toast/Event/ListenerComponent.hpp"
#include "Toast/Physics/Collider.hpp"
#include "Toast/Physics/ColliderFlags.hpp"
#include "Toast/Physics/GravityType.hpp"

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <glm/glm.hpp>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

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

	static auto gravity_type() -> GravityType;
	static auto gravity() -> glm::dvec2;
	static auto gravity_point() -> glm::dvec2;
	static auto gravity_point_scale() -> double;
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
	static toast::Object* PointCollision(glm::vec2 point, ColliderFlags flags);

	static auto GetAllRigidbodies() -> std::list<Rigidbody*>&;
	static auto GetAllCollidingRb() -> std::list<Rigidbody*>&;

	static void SetGravityType(GravityType type);
	static void SetGravityPoint(glm::dvec2 pos);
	static void SetGravityPointScale(double scale);

	static void MainThreadLateTick();

	PhysicsSystem();
	~PhysicsSystem();

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;
	PhysicsSystem(PhysicsSystem&&) = delete;
	PhysicsSystem& operator=(PhysicsSystem&&) = delete;

private:
	enum class MutationType : unsigned char {
		AddRigidbody,
		RemoveRigidbody,
		AddCollider,
		RemoveCollider,
		AddTrigger,
		RemoveTrigger,
		AddBox,
		RemoveBox
	};

	struct PendingMutation {
		MutationType type;
		void* object = nullptr;
		std::shared_ptr<std::promise<void>> completion = nullptr;
	};

	static auto get() noexcept -> std::optional<PhysicsSystem*>;
	static PhysicsSystem* instance;

	void Tick();
	void QueueMutation(PendingMutation mutation);
	void FlushPendingMutations();
	void ApplyPendingMutation(const PendingMutation& mutation);
	[[nodiscard]] bool CanQueueMutations() const;
	[[nodiscard]] bool IsPhysicsThread() const;

	void RigidbodyPhysics(Rigidbody* rb, std::list<std::function<void()>>& localCallbacks);
	void BoxPhysics(BoxRigidbody* rb);

	void CachePhysicsObjects();

	struct M {
		std::chrono::duration<double> targetFrametime { 1.0 / 50.0 };
		unsigned char tickCount = 1;
		std::list<Rigidbody*> colliding;

		std::list<Rigidbody*> rigidbodies;
		std::list<BoxRigidbody*> boxes;
		std::list<ConvexCollider*> colliders;
		std::list<Trigger*> triggers;
		std::recursive_mutex simulationMutex;

		std::list<Rigidbody*> cachedRigidbodies;
		std::list<BoxRigidbody*> cachedBoxRigidbodies;
		std::list<ConvexCollider*> cachedConvexColliders;

		GravityType gravityType = GravityType::DIRECTION;
		glm::dvec2 gravityDirection = { 0.0, -9.81 };
		glm::dvec2 gravityPoint = { 0.0, 0.0 };
		double gravityPointScale = 1.0;
		double positionCorrectionSlop = 1.0e-3;
		double positionCorrectionPtc = 0.4;
		double eps = 1.0e-6;
		double epsSmall = 1.0e-9;

		// Interpolation thingi
		std::atomic<std::chrono::steady_clock::time_point> lastPhysicsTime { std::chrono::steady_clock::now() };

		event::ListenerComponent eventListener;

		std::mutex callbackMutex;
		std::mutex mutationQueueMutex;

		std::list<std::function<void()>> callbackList;
		std::deque<PendingMutation> pendingMutations;
	} m;

	// out of the struct to make sure this is ALWAYS the last
	std::jthread thread;
};

}
