#include "PhysicsSystem.hpp"
#include <Engine/Core/Log.hpp>
#include <Engine/Core/Time.hpp>
#include <Engine/Core/Profiler.hpp>
#include <chrono>

using namespace physics;

#pragma region START_AND_END

PhysicsSystem* PhysicsSystem::instance = nullptr;

auto PhysicsSystem::get() noexcept -> std::optional<PhysicsSystem*> {
	if (instance == nullptr) {
		TOAST_ERROR("Tried to access Physics System before it exists");
		return std::nullopt;
	}

	return instance;
}

void PhysicsSystem::start() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) return;
	if ((*i)->thread.joinable()) return;

	(*i)->thread = std::jthread([this](std::stop_token token) { // NOLINT
		while (!token.stop_requested()) {

			using namespace std::chrono;
			time_point begin = steady_clock::now();

			// Loop the physics simulation a set amount of times per frame
			for (int i = 0; i < m.tickCount; i++) {
				PROFILE_ZONE_N("physics::simulation");
				Time::GetInstance()->PhysTick();
				Tick();

				// Interrupt the loop if we're running out of budget
				duration elapsed = steady_clock::now() - begin;
				if (elapsed >= m.targetFrametime) break;
			}

			duration elapsed = steady_clock::now() - begin;
			if (elapsed < m.targetFrametime) {
				PROFILE_ZONE_NC("physics::wait", 0x404040);
				std::this_thread::sleep_for(m.targetFrametime - elapsed);
			}
		}
	});
}

void PhysicsSystem::stop() {
	auto i = PhysicsSystem::get();
	if (!i.has_value()) return;

	(*i)->thread.request_stop();
	(*i)->thread.join();
}

#pragma endregion

void PhysicsSystem::Tick() {
	PROFILE_ZONE;
}
