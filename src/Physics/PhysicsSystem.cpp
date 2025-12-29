#include "PhysicsSystem.hpp"

#include "Engine/Physics/Collider.hpp"
#include "RigidbodyDynamics.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Profiler.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Toast/World.hpp>
#include <chrono>

using namespace physics;
using namespace glm;

#pragma region START_AND_END

PhysicsSystem* PhysicsSystem::instance = nullptr;

PhysicsSystem::PhysicsSystem() {
	instance = this;
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
	if ((*i)->thread.joinable()) {
		return;
	}

	// TODO: remove at some point
	Collider* test = nullptr;
	if (test) {
		TOAST_TRACE("false");
	}

	(*i)->thread = std::jthread([physics = (*i)](std::stop_token token) {    // NOLINT
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

			duration elapsed = steady_clock::now() - begin;
			if (elapsed < physics->m.targetFrametime) {
				PROFILE_ZONE_NC("physics::wait", 0x404040);
				std::this_thread::sleep_for(physics->m.targetFrametime - elapsed);
			}
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
	if (!physics->thread.joinable()) return;

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

dvec2 PhysicsSystem::gravity() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) {
		return { 0.0, 0.0 };
	}

	return (*i)->m.gravity;
}

#pragma endregion

void PhysicsSystem::RigidbodyPhysics(Rigidbody* rb) {
	PROFILE_ZONE;
	PROFILE_TEXT(rb->parent()->name(), rb->parent()->name().size());

	// RbKinematics(rb);
	//
	// // Collision loops
	// for (auto it = ++std::ranges::find(m.rigidbodies, rb); it != m.rigidbodies.end(); ++it) {
	// 	auto manifold = RbRbCollision(rb, *it);
	// 	if (manifold.has_value()) {
	// 		RbRbResolution(rb, *it, manifold.value());
	// 	}
	// }
	
	for (auto* c : m.colliders) {
		RbMeshCollision(rb, c);
	}
}
