/// @file debris_simulation.hpp
/// @author dario
/// @date 10/10/2026

#pragma once

#include <glm/glm.hpp>
#include <span>
#include <toast/renderer/async_compute_task.hpp>

namespace example {

/// Debris particles integrated on the async compute queue
class DebrisSimulation : public renderer::AsyncComputeTask {
public:
	static constexpr uint32_t k_max_particles = 65536;

	DebrisSimulation();

	/// Physics thread before the first step
	void spawn(std::span<const glm::vec4> positions, std::span<const glm::vec4> velocities);

	/// Physics thread. Returns right away
	void step(float dt);

	/// Physics thread. Newest finished positions and false until one exists
	auto readPositions(std::span<glm::vec4> out) -> bool {
		return read(m_positions, out);
	}

	/// For the render pass. xyz position w radius
	[[nodiscard]]
	auto positionsId() const -> BufferId {
		return m_positions;
	}

private:
	/// Mirrors debris.slang Params
	struct Params {
		float dt;
		uint32_t count;
	};

	BufferId m_positions = 0;
	BufferId m_velocities = 0;
	uint32_t m_count = 0;
};

}
