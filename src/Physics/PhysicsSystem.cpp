#include "PhysicsSystem.hpp"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <Engine/Toast/World.hpp>
#include <optional>
#include <thread>

using namespace physics;

PhysicsSystem* PhysicsSystem::m_instance = nullptr;

auto PhysicsSystem::get() -> std::optional<PhysicsSystem*> {
	if (m_instance == nullptr) {
		TOAST_WARN("Trying to access Physics Systems but it doesn't exist yet");
		return std::nullopt;
	}

	return m_instance;
}

auto PhysicsSystem::create() -> std::optional<PhysicsSystem*> {
	if (m_instance != nullptr) {
		TOAST_ERROR("Trying to create Physics System but it already exists");
		return std::nullopt;
	}

	// creating a temp clone of m_instance for readability
	auto* physics = m_instance = new PhysicsSystem;
	physics->m.threadPool.Init(1);

	// TODO: Move later to a place that can be started/ended properly
	physics->m.threadPool.QueueJob([physics]() {
		while (true) {
			Time::GetInstance()->PhysTick();
			physics->Tick();

			// Wait for the physics to reach its target frame time
			physics->Wait();
		}
	});

	TOAST_INFO("Created Physics System");
	return physics;
}

PhysicsSystem::~PhysicsSystem() {
	m_instance = nullptr;
}

void PhysicsSystem::Wait() const {
	const double target_dt = 1.0f / m.frameTarget;
	const double delta = Time::raw_fixed_delta();
	const double remaining = target_dt - delta;

	if (remaining <= 0.0f) {
		return;
	}
	std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
}

void PhysicsSystem::AddRigidbody(Rigidbody* rb) {
	auto try_get = get();
	if (!try_get.has_value()) {
		return;
	}
	auto& list = try_get.value()->m.rigidbodies;

#ifdef _DEBUG
	// Just to check, but shouldn't happen at all
	if (std::ranges::find(list, rb) != list.end()) {
		TOAST_WARN("Rigidbody from {} already on the list", rb->parent()->name());
	}
#endif
	list.emplace_back(rb);
}

void PhysicsSystem::Tick() {
	// We should apply the physics tick resolution in the following order:
	//		1) PhysTick calls from all objects
	//		2) Rigidbody dynamics
	//		3) Collision detection
	//		4) Collision resolution
	//		5) Dispatching of callbacks

	toast::World::Instance()->PhysTick();

	for (auto& rb : m.rigidbodies) {
		rb->UpdatePosition();
	}

	for (int i = 0; i < m.collisionResolutionCount; i++) {
		// physics_collision_detection();
		// physics_collision_resolution();
	}

	// physics_callback_dispatch();
}
