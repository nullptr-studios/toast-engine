#include "PhysicsSystem.hpp"

#include "Resolutions.h"

#include <Engine/Core/Log.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Physics/Collider.hpp>
#include <Engine/Physics/Rigidbody.hpp>
#include <Engine/Toast/World.hpp>
#include <chrono>
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

	// TODO: Move later to a place that can be started/ended properly
	physics->m.physicsThread = std::jthread([physics]() {
		while (true) {
			using clock = std::chrono::steady_clock;
			std::chrono::duration<double> target { 1.0f / physics->m.frameTarget };
			auto begin = clock::now();

			for (int i = 0; i < physics->m.collisionResolutionCount; i++) {
				Time::GetInstance()->PhysTick();
				physics->Tick();

				auto elapsed = clock::now() - begin;
				if (elapsed >= target) {
					TOAST_WARN("Breaking at {} iterations because phhysics delta is bigger than target", i);
					break;
				}
			}

			// Wait for the physics to reach its target frame time
			auto elapsed = clock::now() - begin;
			if (elapsed < target) {
				std::this_thread::sleep_for(target - elapsed);
			}
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

void PhysicsSystem::Tick() {
	// We should apply the physics tick resolution in the following order:
	//		1) PhysTick calls from all objects
	//		2) Rigidbody dynamics
	//		3) Collision detection
	//		4) Collision resolution
	//		5) Dispatching of callbacks

	toast::World::Instance()->PhysTick();

	std::vector lines = GetColliderLines();
	for (auto& rigidbody : m.rigidbodies) {
		auto rb = rigidbody->data();
		rb.velocity.y -= 9.81f * Time::fixed_delta();

		for (auto& l : lines) {
			_rb_line_collision(rb, l);
		}

		rb.position += rb.velocity * Time::fixed_delta();
		rigidbody->data(rb);
	}

	for (auto it_1 = m.rigidbodies.begin(); it_1 != m.rigidbodies.end(); ++it_1) {
		for (auto it_2 = std::next(it_1); it_2 != m.rigidbodies.end(); ++it_2) {
			auto rb_1 = (*it_1)->data();
			auto rb_2 = (*it_2)->data();
			_rb_rb_collision(rb_1, rb_2);
			(*it_1)->data(rb_1);
			(*it_2)->data(rb_2);
		}
	}

	// physics_callback_dispatch();
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
		return;
	}
#endif
	list.emplace_back(rb);
}

void PhysicsSystem::RemoveRigidbody(Rigidbody* rb) {
	auto try_get = get();
	if (!try_get.has_value()) {
		return;
	}
	auto& list = try_get.value()->m.rigidbodies;
	list.remove(rb);
}

void PhysicsSystem::AddCollider(Collider* c) {
	auto try_get = get();
	if (!try_get.has_value()) {
		return;
	}
	auto& list = try_get.value()->m.colliders;

#ifdef _DEBUG
	// Just to check, but shouldn't happen at all
	if (std::ranges::find(list, c) != list.end()) {
		TOAST_WARN("Collider from {} already on the list", c->parent()->name());
		return;
	}
#endif
	list.emplace_back(c);
}

void PhysicsSystem::RemoveCollider(Collider* c) {
	auto try_get = get();
	if (!try_get.has_value()) {
		return;
	}
	auto& list = try_get.value()->m.colliders;
	list.remove(c);
}

auto PhysicsSystem::GetColliderLines() -> std::vector<Line> {
	return m.colliders | std::views::transform([](const auto& c) {
		       return c->GetLines();
	       }) |
	       std::views::join | std::ranges::to<std::vector<Line>>();
}
