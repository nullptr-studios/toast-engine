/// @file PhysicsSystem.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once

namespace physics {

class PhysicsSystem {
public:
	static void start();
	static void stop();

	PhysicsSystem() = default;
	~PhysicsSystem() = default;

	PhysicsSystem(const PhysicsSystem&) = delete;
	PhysicsSystem& operator=(const PhysicsSystem&) = delete;
	PhysicsSystem(PhysicsSystem&&) = delete;
	PhysicsSystem& operator=(PhysicsSystem&&) = delete;

private:
	static auto get() noexcept -> std::optional<PhysicsSystem*>;
	static PhysicsSystem* instance;

	void Tick();

	struct M {
		std::chrono::duration<double> targetFrametime {1.0/50.0};
		unsigned char tickCount = 1;
	} m;

	// out of the struct to make sure this is ALWAYS the last
	std::jthread thread;
};


}
