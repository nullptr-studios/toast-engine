#define GLM_ENABLE_EXPERIMENTAL
#include "PhysicsSystem.hpp"

#include "ConvexCollider.hpp"
#include "Physics/BoxDynamics.hpp"
#include "RigidbodyDynamics.hpp"
#include "Toast/Log.hpp"
#include "Toast/Physics/BoxRigidbody.hpp"
#include "Toast/Physics/PhysicsEvents.hpp"
#include "Toast/Physics/Raycast.hpp"
#include "Toast/Physics/Rigidbody.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"
#include "Toast/Renderer/OpenGL/OpenGLRenderer.hpp"
#include "Toast/Time.hpp"
#include "Toast/World.hpp"
#include "glm/gtx/quaternion.hpp"

#include <chrono>

namespace physics {
using namespace glm;

#pragma region START_AND_END

PhysicsSystem* PhysicsSystem::instance = nullptr;

PhysicsSystem::PhysicsSystem() {
	instance = this;

	m.eventListener.Subscribe<UpdatePhysicsDefaults>([this](auto* e) {
		m.gravity = e->gravity;
		m.positionCorrectionPtc = e->positionCorrectionPtc;
		m.positionCorrectionSlop = e->positionCorrectionSlop;
		m.eps = e->eps;
		m.epsSmall = e->epsSmall;
		return true;
	});
}

PhysicsSystem::~PhysicsSystem() {
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
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto* physics = i.value();
	if (physics->thread.joinable()) {
		return;
	}

	physics->thread = std::jthread([physics](std::stop_token token) {    // NOLINT
		while (!token.stop_requested()) {
			using namespace std::chrono;
			time_point begin = steady_clock::now();

			// Loop the physics simulation a set amount of times per frame
			for (int i = 0; i < physics->m.tickCount; i++) {
				PROFILE_ZONE_N("physics::simulation");
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

		// When we stop the physics thread, restore rigidbody velocities
		for (auto* rb : physics->m.rigidbodies) {
			RbResetVelocity(rb);
		}

		for (auto* rb : physics->m.boxes) {
			BoxResetVelocity(rb);
		}
	});
}

void PhysicsSystem::stop() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}

	auto* physics = *i;

	// If the thread is not running, skip
	if (!physics->thread.joinable()) {
		return;
	}

	physics->thread.request_stop();
	physics->thread.join();
}

#pragma endregion

void PhysicsSystem::Tick() {
	PROFILE_ZONE;

	// Propagate the PhysTick down the object tree as first
	toast::World::Instance()->PhysTick();

	// Handle Rigidbody physics
	for (auto* rb : m.rigidbodies) {
		RigidbodyPhysics(rb);
	}

	// Handle Box physics
	for (auto* rb : m.boxes) {
		BoxPhysics(rb);
	}
}

#pragma region HELPER_FUNCTIONS

void PhysicsSystem::AddRigidbody(Rigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto& list = (*i)->m.rigidbodies;

	// Return if the rigidbody is already registered on the list
	if (std::ranges::find(list, rb) != list.end()) {
		return;
	}
	list.emplace_back(rb);
}

void PhysicsSystem::RemoveRigidbody(Rigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	(*i)->m.rigidbodies.remove(rb);
}

void PhysicsSystem::AddCollider(ConvexCollider* c) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto& list = (*i)->m.colliders;

	// Return if the rigidbody is already registered on the list
	if (std::ranges::find(list, c) != list.end()) {
		return;
	}
	list.emplace_back(c);
}

void PhysicsSystem::RemoveCollider(ConvexCollider* c) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	(*i)->m.colliders.remove(c);
}

void PhysicsSystem::AddBox(BoxRigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	auto& list = (*i)->m.boxes;

	// Return if the rigidbody is already registered on the list
	if (std::ranges::find(list, rb) != list.end()) {
		return;
	}
	list.emplace_back(rb);
}

void PhysicsSystem::RemoveBox(BoxRigidbody* rb) {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return;
	}
	(*i)->m.boxes.remove(rb);
}

dvec2 PhysicsSystem::gravity() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return { 0.0, 0.0 };
	}

	return (*i)->m.gravity;
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

void PhysicsSystem::RigidbodyPhysics(Rigidbody* rb) {
	PROFILE_ZONE;
	const auto& name = rb->parent()->name();
	PROFILE_TEXT(name.c_str(), name.size());

	RbKinematics(rb);

	// Collision loops

	for (auto it = ++std::ranges::find(m.rigidbodies, rb); it != m.rigidbodies.end(); ++it) {
		auto manifold = RbRbCollision(rb, *it);
		if (manifold.has_value()) {
			RbRbResolution(rb, *it, manifold.value());
		}
	}

	// TODO: Collision with Boxes

	// TODO: Collision with Triggers

	for (auto* c : m.colliders) {
		auto manifold = RbMeshCollision(rb, c);
		if (manifold.has_value()) {
			RbMeshResolution(rb, c, manifold.value());
		}
	}

	// Final position integration
	RbIntegration(rb);
}

void PhysicsSystem::BoxPhysics(BoxRigidbody* rb) {
	PROFILE_ZONE;
	//PROFILE_TEXT(rb->parent()->name(), rb->parent()->name().size());

	BoxKinematics(rb);

	// Collision loops
	for (auto* c : m.colliders) {
		auto manifold = BoxMeshCollision(rb, c);
		if (manifold.has_value()) {
			BoxMeshResolution(rb, c, manifold.value());
		}
	}

	// Final position integration
	BoxIntegration(rb);
}

std::optional<RayResult> PhysicsSystem::RayCollision(Line* ray) {
	if (not get().has_value()) {
		TOAST_WARN("Raycast skipped because physics system doesn't exist");
		return std::nullopt;
	}
	auto* physics = get().value();

	std::optional<RayResult> result = std::nullopt;
	std::optional<dvec2> col_hit;
	std::optional<dvec2> rb_hit;

	for (auto* c : physics->m.colliders) {
		std::optional<dvec2> collision = ConvexRayCollision(ray, c);
		if (not collision.has_value()) { continue; }

		if (not col_hit.has_value() || length2(*collision - ray->p1) < length2(*col_hit - ray->p1)) {
			col_hit = collision.value();
			const float d = static_cast<float>(distance(*col_hit, ray->p1));

			// same as below
			if (result && result.value().distance < d) continue;
			result = {
				.type = RayResult::Collider,
				.point = *col_hit,
				.distance = d,
				.other = c->parent
			};
		}
	}

	for (auto* r : physics->m.rigidbodies) {
		std::optional<dvec2> collision = RbRayCollision(ray, r);
		if (not collision.has_value()) { continue; }

		if (not rb_hit.has_value() || length(*collision - ray->p1) < length2(*rb_hit - ray->p1)) {
			rb_hit = collision.value();
			const float d = static_cast<float>(distance(*rb_hit, ray->p1));

			// Do not modify result if theres already one with less distance
			if (result && result.value().distance < d) continue;
			result = {
				.type = RayResult::Rigidbody,
				.point = *rb_hit,
				.distance = d,
				.other = r->parent()
			};
		}
	}
	if (result != std::nullopt)
		renderer::DebugLine(ray->p1,  result->point, vec4(0.0f, 0.0f, 1.0f, 1.0f));

	return std::nullopt;
}
}
