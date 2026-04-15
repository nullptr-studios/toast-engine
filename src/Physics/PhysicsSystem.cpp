#define GLM_ENABLE_EXPERIMENTAL

// #define TRACY_FIBERS

#include "PhysicsSystem.hpp"

#include "ConvexCollider.hpp"
#include "Physics/BoxDynamics.hpp"
#include "RigidbodyDynamics.hpp"
#include "Toast/Log.hpp"
#include "Toast/Physics/BoxRigidbody.hpp"
#include "Toast/Physics/GravityType.hpp"
#include "Toast/Physics/Physics.hpp"
#include "Toast/Physics/PhysicsEvents.hpp"
#include "Toast/Physics/Raycast.hpp"
#include "Toast/Physics/Rigidbody.hpp"
#include "Toast/Physics/Trigger.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"
#include "Toast/Time.hpp"
#include "Toast/World.hpp"
#include "glm/gtx/quaternion.hpp"

#include <chrono>

namespace physics {
using namespace glm;

auto GravityType::FromString(std::string_view other) -> GravityType {
	std::string str = other.data();
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	if (str == "direction") {
		return GravityType::DIRECTION;
	}
	if (str == "point") {
		return GravityType::POINT;
	}

	TOAST_WARN("Value \"{}\" not recognized as valid GravityType", str);
	return GravityType::DIRECTION;
}

auto GravityType::ToString(GravityType other) -> std::string {
	switch (other.v) {
		case DIRECTION: return "Direction";
		case POINT: return "Point";
		default: return "Unknown";
	}
}

auto GravityType::operator=(type value) -> GravityType& {
	v = value;
	return *this;
}

bool GravityType::operator==(type value) const {
	return v == value;
}

bool GravityType::operator==(const GravityType& other) const {
	return v == other.v;
}

#pragma region START_AND_END

PhysicsSystem* PhysicsSystem::instance = nullptr;
static std::mutex g_threadLifecycleMutex;
static std::atomic<bool> g_threadAlive { false };
static std::atomic<bool> g_intentionallyStopped { false };

PhysicsSystem::PhysicsSystem() {
	instance = this;

	m.eventListener.Subscribe<UpdatePhysicsDefaults>([this](auto* e) {
		m.gravityDirection = e->gravity;
		m.positionCorrectionPtc = e->positionCorrectionPtc;
		m.positionCorrectionSlop = e->positionCorrectionSlop;
		m.eps = e->eps;
		m.epsSmall = e->epsSmall;
		m.tickCount = e->iterationCount;
		return true;
	});

	// Ensure the intentionally-stopped flag is false by default for newly constructed system
	g_intentionallyStopped.store(false, std::memory_order_release);
}

PhysicsSystem::~PhysicsSystem() {
	// Ensure threads are stopped and supervisor won't respawn them
	PhysicsSystem::stop();

	std::lock_guard lock(g_threadLifecycleMutex);
	instance = nullptr;
}

auto PhysicsSystem::get() noexcept -> std::optional<PhysicsSystem*> {
	if (instance == nullptr) {
		TOAST_ERROR("Tried to access Physics System before it exists");
		return std::nullopt;
	}

	return instance;
}

void PhysicsSystem::start() {
	std::lock_guard lock(g_threadLifecycleMutex);
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();

	// Clear intentional stop so supervisor may restart the thread if needed
	g_intentionallyStopped.store(false, std::memory_order_release);

	// If thread is already running, nothing to do
	if (g_threadAlive.load(std::memory_order_acquire)) {
		return;
	}

	// If there is an old thread object, join it to clean up resources
	if (physics->thread.joinable()) {
		try {
			physics->thread.join();
		} catch (...) { }
	}

	physics->thread = std::jthread([physics](std::stop_token token) {    // NOLINT
		g_threadAlive.store(true, std::memory_order_release);
		try {
			// TracyFiberEnter("Physics Thread");
			while (!token.stop_requested()) {
				using namespace std::chrono;
				time_point begin = steady_clock::now();
				PROFILE_ZONE_N("physics::simulation");

				// Apply queued add/remove requests on the physics thread before
				// building this frame's cache to keep pointer lifetimes safe
				physics->FlushPendingMutations();

				// Loop the physics simulation a set amount of times per frame
				physics->CachePhysicsObjects();
				for (int i = 0; i < physics->m.tickCount; i++) {
					Time::GetInstance()->PhysTick();
					physics->Tick();

					// Interrupt the loop if we're running out of budget
					duration elapsed = steady_clock::now() - begin;
					if (elapsed >= physics->m.targetFrametime) {
						break;
					}
				}

				// Handle constant frame time
				duration elapsed = steady_clock::now() - begin;
				if (elapsed < physics->m.targetFrametime) {
					PROFILE_ZONE_NC("physics::wait", 0x404040);
					std::this_thread::sleep_for(physics->m.targetFrametime - elapsed);
				}
			}
		} catch (const std::exception& e) { TOAST_ERROR("Physics thread crashed with exception: {}", e.what()); } catch (...) {
			TOAST_ERROR("Physics thread crashed with unknown exception");
		}

		// When we stop the physics thread, restore rigidbody velocities
		{
			std::lock_guard sim_lock(physics->m.simulationMutex);
			for (auto* rb : physics->m.rigidbodies) {
				RbResetVelocity(rb);
			}

			for (auto* rb : physics->m.boxes) {
				BoxResetVelocity(rb);
			}
		}

		// Mark thread as no longer alive
		g_threadAlive.store(false, std::memory_order_release);
		// TracyFiberLeave;
	});
}

void PhysicsSystem::stop() {
	PhysicsSystem* physicsPtr = nullptr;
	bool threadJoinable = false;
	{
		std::lock_guard lock(g_threadLifecycleMutex);
		auto i = PhysicsSystem::get();
		if (!i.has_value()) {
			return;
		}
		physicsPtr = *i;
		// Mark as intentionally stopped so main-thread supervisor doesn't respawn the thread
		g_intentionallyStopped.store(true, std::memory_order_release);
		threadJoinable = physicsPtr->thread.joinable();
		if (threadJoinable) {
			physicsPtr->thread.request_stop();
		}
	}

	// Join outside of the lifecycle mutex to avoid deadlocks
	if (threadJoinable) {
		try {
			physicsPtr->thread.join();
		} catch (...) { }
	}
	g_threadAlive.store(false, std::memory_order_release);

	if (physicsPtr != nullptr) {
		physicsPtr->FlushPendingMutations();
	}
}

#pragma endregion

void PhysicsSystem::Tick() {
	PROFILE_ZONE;

	std::list<std::function<void()>> localCallbacks;

	{
		std::lock_guard sim_lock(m.simulationMutex);

		// Store previous positions for interpolation before physics step
		for (auto* rb : m.cachedRigidbodies) {
			rb->StorePreviousPosition();
		}

		// Propagate the PhysTick down the object tree as first
		toast::World::Instance()->PhysTick();

		// Handle Rigidbody physics
		for (auto* rb : m.cachedRigidbodies) {
			RigidbodyPhysics(rb, localCallbacks);
		}

		// Handle Box physics
		for (auto* rb : m.cachedBoxRigidbodies) {
			BoxPhysics(rb);
		}
	}

	// Record the time of this physics step for interpolation
	m.lastPhysicsTime.store(std::chrono::steady_clock::now(), std::memory_order_release);

	// Append local callbacks to the global list after releasing the simulation lock
	if (!localCallbacks.empty()) {
		std::lock_guard lock(m.callbackMutex);
		m.callbackList.splice(m.callbackList.end(), localCallbacks);
	}
}

#pragma region HELPER_FUNCTIONS

void PhysicsSystem::QueueMutation(PendingMutation mutation) {
	std::lock_guard lock(m.mutationQueueMutex);
	m.pendingMutations.emplace_back(std::move(mutation));
}

void PhysicsSystem::FlushPendingMutations() {
	std::deque<PendingMutation> localMutations;
	{
		std::lock_guard queue_lock(m.mutationQueueMutex);
		if (m.pendingMutations.empty()) {
			return;
		}
		localMutations.swap(m.pendingMutations);
	}

	std::vector<std::shared_ptr<std::promise<void>>> completions;
	{
		std::lock_guard sim_lock(m.simulationMutex);
		for (auto& mutation : localMutations) {
			ApplyPendingMutation(mutation);
			if (mutation.completion != nullptr) {
				completions.emplace_back(std::move(mutation.completion));
			}
		}
	}

	for (const auto& completion : completions) {
		completion->set_value();
	}
}

void PhysicsSystem::ApplyPendingMutation(const PendingMutation& mutation) {
	switch (mutation.type) {
		case MutationType::AddRigidbody: {
			auto* rb = static_cast<Rigidbody*>(mutation.object);
			if (std::ranges::find(m.rigidbodies, rb) == m.rigidbodies.end()) {
				m.rigidbodies.emplace_back(rb);
			}
			break;
		}

		case MutationType::RemoveRigidbody: {
			auto* rb = static_cast<Rigidbody*>(mutation.object);
			m.rigidbodies.remove(rb);
			m.colliding.remove(rb);
			break;
		}

		case MutationType::AddCollider: {
			auto* collider = static_cast<ConvexCollider*>(mutation.object);
			if (std::ranges::find(m.colliders, collider) == m.colliders.end()) {
				m.colliders.emplace_back(collider);
			}
			break;
		}

		case MutationType::RemoveCollider: {
			auto* collider = static_cast<ConvexCollider*>(mutation.object);
			m.colliders.remove(collider);
			break;
		}

		case MutationType::AddTrigger: {
			auto* trigger = static_cast<Trigger*>(mutation.object);
			if (std::ranges::find(m.triggers, trigger) == m.triggers.end()) {
				m.triggers.emplace_back(trigger);
			}
			break;
		}

		case MutationType::RemoveTrigger: {
			auto* trigger = static_cast<Trigger*>(mutation.object);
			m.triggers.remove(trigger);
			break;
		}

		case MutationType::AddBox: {
			auto* box = static_cast<BoxRigidbody*>(mutation.object);
			if (std::ranges::find(m.boxes, box) == m.boxes.end()) {
				m.boxes.emplace_back(box);
			}
			break;
		}

		case MutationType::RemoveBox: {
			auto* box = static_cast<BoxRigidbody*>(mutation.object);
			m.boxes.remove(box);
			break;
		}
	}
}

bool PhysicsSystem::CanQueueMutations() const {
	if (!g_threadAlive.load(std::memory_order_acquire)) {
		return false;
	}

	if (!thread.joinable()) {
		return false;
	}

	return !thread.get_stop_token().stop_requested();
}

bool PhysicsSystem::IsPhysicsThread() const {
	if (!thread.joinable()) {
		return false;
	}

	return thread.get_id() == std::this_thread::get_id();
}

void PhysicsSystem::AddRigidbody(Rigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::AddRigidbody, .object = rb };

	if (physics->CanQueueMutations()) {
		physics->QueueMutation(std::move(mutation));
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::RemoveRigidbody(Rigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::RemoveRigidbody, .object = rb };

	if (physics->CanQueueMutations()) {
		if (physics->IsPhysicsThread()) {
			physics->QueueMutation(std::move(mutation));
			return;
		}

		auto completion = std::make_shared<std::promise<void>>();
		auto finished = completion->get_future();
		mutation.completion = completion;
		physics->QueueMutation(std::move(mutation));
		finished.wait();
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::AddCollider(ConvexCollider* c) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::AddCollider, .object = c };

	if (physics->CanQueueMutations()) {
		physics->QueueMutation(std::move(mutation));
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::RemoveCollider(ConvexCollider* c) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::RemoveCollider, .object = c };

	if (physics->CanQueueMutations()) {
		if (physics->IsPhysicsThread()) {
			physics->QueueMutation(std::move(mutation));
			return;
		}

		auto completion = std::make_shared<std::promise<void>>();
		auto finished = completion->get_future();
		mutation.completion = completion;
		physics->QueueMutation(std::move(mutation));
		finished.wait();
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::AddTrigger(Trigger* t) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::AddTrigger, .object = t };

	if (physics->CanQueueMutations()) {
		physics->QueueMutation(std::move(mutation));
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::RemoveTrigger(Trigger* t) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::RemoveTrigger, .object = t };

	if (physics->CanQueueMutations()) {
		if (physics->IsPhysicsThread()) {
			physics->QueueMutation(std::move(mutation));
			return;
		}

		auto completion = std::make_shared<std::promise<void>>();
		auto finished = completion->get_future();
		mutation.completion = completion;
		physics->QueueMutation(std::move(mutation));
		finished.wait();
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::AddBox(BoxRigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::AddBox, .object = rb };

	if (physics->CanQueueMutations()) {
		physics->QueueMutation(std::move(mutation));
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

void PhysicsSystem::RemoveBox(BoxRigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	PendingMutation mutation { .type = MutationType::RemoveBox, .object = rb };

	if (physics->CanQueueMutations()) {
		if (physics->IsPhysicsThread()) {
			physics->QueueMutation(std::move(mutation));
			return;
		}

		auto completion = std::make_shared<std::promise<void>>();
		auto finished = completion->get_future();
		mutation.completion = completion;
		physics->QueueMutation(std::move(mutation));
		finished.wait();
		return;
	}

	std::lock_guard sim_lock(physics->m.simulationMutex);
	physics->ApplyPendingMutation(mutation);
}

auto PhysicsSystem::gravity_type() -> GravityType {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return GravityType::DIRECTION;
	}

	return (*i)->m.gravityType;
}

dvec2 PhysicsSystem::gravity() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return { 0.0, 0.0 };
	}

	return (*i)->m.gravityDirection;
}

auto PhysicsSystem::gravity_point() -> glm::dvec2 {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return { 0.0, 0.0 };
	}

	return (*i)->m.gravityPoint;
}

auto PhysicsSystem::gravity_point_scale() -> double {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 0.0;
	}

	return (*i)->m.gravityPointScale;
}

double PhysicsSystem::pos_slop() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 0.0;
	}

	return (*i)->m.positionCorrectionSlop;
}

double PhysicsSystem::pos_ptc() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 0.0;
	}

	return (*i)->m.positionCorrectionPtc;
}

double PhysicsSystem::eps() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 0.0;
	}

	return (*i)->m.eps;
}

double PhysicsSystem::eps_small() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 0.0;
	}

	return (*i)->m.epsSmall;
}

#pragma endregion

void PhysicsSystem::UpdateVisualInterpolation() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = *i;
	std::lock_guard sim_lock(physics->m.simulationMutex);

	// Calculate time elapsed since last physics step
	auto now = std::chrono::steady_clock::now();
	auto last_physics = physics->m.lastPhysicsTime.load(std::memory_order_acquire);
	std::chrono::duration<double> elapsed = now - last_physics;

	// Calculate interpolation alpha
	double fixed_dt = physics->m.targetFrametime.count();
	double alpha = glm::clamp(elapsed.count() / fixed_dt, 0.0, 1.0);

	// Update global interpolation alpha
	Rigidbody::UpdateInterpolationAlpha(alpha);

	// Update all rigidbody visual transforms
	for (auto* rb : physics->m.rigidbodies) {
		rb->UpdateVisualTransform();
	}
}

double PhysicsSystem::GetFixedTimestep() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return 1.0 / 120.0;
	}
	return (*i)->m.targetFrametime.count();
}

void PhysicsSystem::RigidbodyPhysics(Rigidbody* rb, std::list<std::function<void()>>& localCallbacks) {
	if (not rb->enabled()) {
		return;
	}

	PROFILE_ZONE;
	const auto& name = rb->parent()->name();
	PROFILE_TEXT(name.c_str(), name.size());

	RbKinematics(rb);

	// Collision loops - operate only on cached (visible/active) physics lists to improve performance

	// Rigidbody vs Rigidbody (only iterate cached rigidbodies and only those after this one to avoid double checks)
	{
		auto cached_it = std::ranges::find(m.cachedRigidbodies, rb);
		if (cached_it != m.cachedRigidbodies.end()) {
			for (auto it = std::next(cached_it); it != m.cachedRigidbodies.end(); ++it) {
				if (not(*it)->enabled()) {
					continue;
				}

				auto manifold = RbRbCollision(rb, *it);
				if (manifold.has_value()) {
					RbRbResolution(rb, *it, manifold.value());

					if (rb->enterCallback && rb->CanCallBack(*it)) {
						auto* other = *it;
						localCallbacks.emplace_back([rb, other]() {
							rb->enterCallback(other);
						});
					}

					if ((*it)->enterCallback && (*it)->CanCallBack(rb)) {
						auto* other = *it;
						localCallbacks.emplace_back([rb, other]() {
							other->enterCallback(rb);
						});
					}

					if (std::ranges::find(m.colliding, rb) != m.colliding.end()) {
						m.colliding.emplace_back(rb);
					}

					if (std::ranges::find(m.colliding, *it) != m.colliding.end()) {
						m.colliding.emplace_back(*it);
					}
				} else {
					auto find = std::ranges::find(m.colliding, rb);
					if (find != m.colliding.end()) {
						auto* other = *it;
						localCallbacks.emplace_back([rb, other]() {
							rb->exitCallback(other);
						});
						m.colliding.erase(find);
					}

					find = std::ranges::find(m.colliding, *it);
					if (find != m.colliding.end()) {
						auto* other = *it;
						localCallbacks.emplace_back([rb, other]() {
							other->exitCallback(rb);
						});
						m.colliding.erase(find);
					}
				}
			}
		}
	}

	// Rigidbody vs Box (only cached boxes)
	for (auto* b : m.cachedBoxRigidbodies) {
		auto manifold = RbBoxCollision(rb, b);
		if (manifold.has_value()) {
			RbBoxResolution(rb, b, manifold.value());
		}
	}

	// Rigidbody vs Convex Colliders (only cached convex colliders)
	for (auto* c : m.cachedConvexColliders) {
		if (not c->parent->enabled()) {
			continue;
		}

		auto manifold = RbMeshCollision(rb, c);
		if (manifold.has_value()) {
			RbMeshResolution(rb, c, manifold.value());
		}
	}

	for (auto* t : m.triggers) {
		if (not t->enabled()) {
			continue;
		}

		RbTriggerCollision(rb, t, localCallbacks);
	}

	// Final position integration
	RbIntegration(rb);
}

void PhysicsSystem::BoxPhysics(BoxRigidbody* rb) {
	if (not rb->enabled()) {
		return;
	}

	PROFILE_ZONE;
	// PROFILE_TEXT(rb->parent()->name(), rb->parent()->name().size());

	BoxKinematics(rb);

	// Collision loops - operate only on cached (visible/active) physics lists to improve performance

	// Box vs Convex Colliders (only cached convex colliders)
	for (auto* c : m.cachedConvexColliders) {
		if (not c->parent->enabled()) {
			continue;
		}

		auto manifold = BoxMeshCollision(rb, c);
		if (manifold.has_value()) {
			BoxMeshResolution(rb, c, manifold.value());
		}
	}

	// Box vs Box (only iterate cached boxes and only those after this one to avoid double checks)
	{
		auto cached_it = std::ranges::find(m.cachedBoxRigidbodies, rb);
		if (cached_it != m.cachedBoxRigidbodies.end()) {
			for (auto it = std::next(cached_it); it != m.cachedBoxRigidbodies.end(); ++it) {
				auto manifold = BoxBoxCollision(rb, *it);
				if (manifold.has_value()) {
					BoxBoxResolution(rb, *it, manifold.value());
				}
			}
		}
	}

	// Final position integration
	BoxIntegration(rb);
}

void PhysicsSystem::CachePhysicsObjects() {
	std::lock_guard sim_lock(m.simulationMutex);

	// detects if inside frustum or out, cached lists contains in screen physics
	auto rb_view = m.rigidbodies | std::views::filter([](auto* rb) -> bool {
		               if (rb->m_skipBoundsCheck) {
			               return true;
		               }
		               auto pstition = rb->GetPosition();
		               return OclussionVolume::isSphereOnPlanes(glm::vec3(pstition.x, pstition.y, 0.f), rb->radius);
	               });
	m.cachedRigidbodies.assign(rb_view.begin(), rb_view.end());

	auto box_view = m.boxes | std::views::filter([](auto* rb) -> bool {
		                return OclussionVolume::isAABBOnPlanes(rb->GetAABB());
	                });
	m.cachedBoxRigidbodies.assign(box_view.begin(), box_view.end());

	// Cache visible convex colliders for this simulation frame.
	auto convex_view = m.colliders | std::views::filter([](auto* rb) -> bool {
		                   return OclussionVolume::isAABBOnPlanes(rb->getAABB());
	                   });
	m.cachedConvexColliders.assign(convex_view.begin(), convex_view.end());
}

std::optional<RayResult> PhysicsSystem::RayCollision(Line* ray, ColliderFlags flags) {
	if (not get().has_value()) {
		TOAST_WARN("Raycast skipped because physics system doesn't exist");
		return std::nullopt;
	}
	auto* physics = get().value();
	std::lock_guard sim_lock(physics->m.simulationMutex);

	std::optional<RayResult> result = std::nullopt;
	std::optional<dvec2> col_hit;
	std::optional<dvec2> rb_hit;
	Rigidbody* rigidbody = nullptr;

	for (auto* c : physics->m.colliders) {
		if (!c->parent->enabled()) {
			continue;
		}

		if ((static_cast<unsigned int>(flags) & static_cast<unsigned int>(c->flags)) == 0u) {
			continue;
		}
		auto collision = ConvexRayCollision(ray, c);
		if (not collision.has_value()) {
			continue;
		}

		if (not col_hit.has_value() || length2(collision->first - ray->p1) < length2(*col_hit - ray->p1)) {
			col_hit = collision->first;
			const float d = static_cast<float>(distance(*col_hit, ray->p1));

			// same as below
			if (result && result.value().distance < d) {
				continue;
			}
			result = { .type = RayResult::Collider, .point = *col_hit, .normal = collision->second, .distance = d, .other = c->parent };
		}
	}



	for (auto* r : physics->m.rigidbodies) {
		if (!r->enabled()) {
			continue;
		}

		if ((static_cast<unsigned int>(flags) & static_cast<unsigned int>(r->flags)) == 0u) {
			continue;
		}
		std::optional<dvec2> collision = RbRayCollision(ray, r);
		if (not collision.has_value()) {
			continue;
		}

		if (not rb_hit.has_value() || length(*collision - ray->p1) < length2(*rb_hit - ray->p1)) {
			rb_hit = collision.value();
			const float d = static_cast<float>(distance(*rb_hit, ray->p1));

			// Do not modify result if theres already one with less distance
			if (result && result.value().distance < d) {
				continue;
			}
			result = { .type = RayResult::Rigidbody, .point = *rb_hit, .normal = ray->tangent, .distance = d, .other = r->parent() };
			rigidbody = r;
		}
	}

	for (auto* c : physics->m.boxes) {
		
		auto collision = BoxRayCollision(ray, c);
		if (not collision.has_value()) {
			continue;
		}

		if (not col_hit.has_value() || length2(collision->first - ray->p1) < length2(*col_hit - ray->p1)) {
			col_hit = collision->first;
			const float d = static_cast<float>(distance(*col_hit, ray->p1));

			// same as below
			if (result && result.value().distance < d) {
				continue;
			}
			result = { .type = RayResult::Box, .point = *col_hit, .normal = collision->second, .distance = d, .other = c->parent };
		}
	}

	if (rigidbody != nullptr) {
		if (rigidbody->enterCallback) {
			rigidbody->enterCallback(rigidbody);
		}
	}
	return result;
}

toast::Object* PhysicsSystem::PointCollision(glm::vec2 point, ColliderFlags flags) {
	if (not get().has_value()) {
		return nullptr;
	}
	auto* physics = get().value();
	std::lock_guard sim_lock(physics->m.simulationMutex);

	const dvec2 pt { point };

	// Colliders
	for (auto* c : physics->m.colliders) {
		if (!c->parent->enabled()) {
			continue;
		}
		if ((static_cast<unsigned int>(flags) & static_cast<unsigned int>(c->flags)) == 0u) {
			continue;
		}
		if (c->edges.empty()) {
			continue;
		}

		bool inside = true;
		for (const auto& edge : c->edges) {
			dvec2 to_point = pt - edge.p1;
			// If dot with the outward normal is positive, point is outside this edge
			if (glm::dot(to_point, edge.normal) > 0.0) {
				inside = false;
				break;
			}
		}
		if (inside) {
			return c->parent->parent();
		}
	}

	// Check rigidbodies
	for (auto* rb : physics->m.rigidbodies) {
		if (!rb->enabled()) {
			continue;
		}
		if ((static_cast<unsigned int>(flags) & static_cast<unsigned int>(rb->flags)) == 0u) {
			continue;
		}
		if (glm::length2(pt - rb->GetPosition()) <= rb->radius * rb->radius) {
			return rb->parent();
		}
	}

	return nullptr;
}

auto PhysicsSystem::GetAllRigidbodies() -> std::list<Rigidbody*>& {
	auto i = PhysicsSystem::get();
	// high cortison std::optional vs low cortison c assert
	assert(i.has_value() && "Physics System does not exist");
	std::lock_guard sim_lock(i.value()->m.simulationMutex);
	return i.value()->m.rigidbodies;
}

auto PhysicsSystem::GetAllCollidingRb() -> std::list<Rigidbody*>& {
	auto i = PhysicsSystem::get();
	// high cortison std::optional vs low cortison c assert
	assert(i.has_value() && "Physics System does not exist");
	std::lock_guard sim_lock(i.value()->m.simulationMutex);
	return i.value()->m.colliding;
}

void PhysicsSystem::SetGravityType(GravityType type) {
	auto i = PhysicsSystem::get();
	if (not i.has_value()) {
		return;
	}

	i.value()->m.gravityType = type;
}

void PhysicsSystem::SetGravityPoint(glm::dvec2 pos) {
	auto i = PhysicsSystem::get();
	if (not i.has_value()) {
		return;
	}

	i.value()->m.gravityPoint = pos;
}

void PhysicsSystem::SetGravityPointScale(double scale) {
	auto i = PhysicsSystem::get();
	if (not i.has_value()) {
		return;
	}

	i.value()->m.gravityPointScale = scale;
}

void PhysicsSystem::MainThreadLateTick() {
	auto i = PhysicsSystem::get();

	if (!i.has_value()) {
		return;
	}

	std::lock_guard lock(i.value()->m.callbackMutex);
	{
		for (const auto& callback : i.value()->m.callbackList) {
			callback();
		}
		i.value()->m.callbackList.clear();
	}

	// Main-thread supervisor: restart physics thread on unexpected exit unless intentionally stopped
	if (!g_intentionallyStopped.load(std::memory_order_acquire) && !g_threadAlive.load(std::memory_order_acquire)) {
		PhysicsSystem::start();
	}
}

void SetGravityType(GravityType type) {
	PhysicsSystem::SetGravityType(type);
}

void SetGravityPoint(glm::dvec2 pos) {
	PhysicsSystem::SetGravityPoint(pos);
}

void SetGravityPointScale(double scale) {
	PhysicsSystem::SetGravityPointScale(scale);
}

auto GetAllRigidbodies() -> std::list<Rigidbody*>& {
	return PhysicsSystem::GetAllRigidbodies();
}

auto Gravity() -> float {
	return glm::length(PhysicsSystem::gravity());
}

}
