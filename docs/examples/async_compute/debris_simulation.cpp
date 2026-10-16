/// @file debris_simulation.cpp
/// @author dario
/// @date 10/10/2026

#include "debris_simulation.hpp"

#include <algorithm>

namespace example {

namespace {
constexpr vk::DeviceSize k_particle_bytes = sizeof(glm::vec4) * DebrisSimulation::k_max_particles;
constexpr uint32_t k_workgroup_size = 64;
}

DebrisSimulation::DebrisSimulation()
    : AsyncComputeTask({.shader_uri = "core://shaders/debris.slang", .entries = {"integrateMain"}, .debug_name = "Debris"}) {
	// The renderer draws positions and gameplay reads them back. Velocities never leave the task
	m_positions = addRingBuffer(k_particle_bytes, true);
	m_velocities = addPersistentBuffer(k_particle_bytes);

	// Mirrors the bindings in debris.slang
	bind(0, m_positions, Copy::previous);
	bind(1, m_positions, Copy::current);
	bind(2, m_velocities);
}

void DebrisSimulation::spawn(std::span<const glm::vec4> positions, std::span<const glm::vec4> velocities) {
	m_count = static_cast<uint32_t>(std::min({positions.size(), velocities.size(), size_t {k_max_particles}}));
	upload(m_positions, positions.first(m_count));
	upload(m_velocities, velocities.first(m_count));
}

void DebrisSimulation::step(float dt) {
	if (m_count == 0) {
		return;
	}
	const Params params {.dt = dt, .count = m_count};
	AsyncComputeTask::step([&](StepContext& ctx) { ctx.dispatch((m_count + k_workgroup_size - 1) / k_workgroup_size, params); });
}

}
