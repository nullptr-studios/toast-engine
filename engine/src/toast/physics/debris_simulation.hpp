/**
 * @file debris_simulation.hpp
 * @author Xein
 * @date 19 Oct 2026
 * @brief Small voxel debris
 */

#pragma once

#include "debris.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <span>
#include <toast/renderer/async_compute_task.hpp>

namespace renderer {
struct VoxelCollisionWorld;
}

namespace physics {

class TOAST_API DebrisSimulation : public renderer::AsyncComputeTask {
public:
	struct GpuParticle {
		glm::vec3 position {0.0f};
		uint32_t packed = 0;
		glm::vec4 rotation {0.0f, 0.0f, 0.0f, 1.0f};
		glm::vec3 velocity {0.0f};
		uint32_t albedo_roughness = 0;
	};

	struct GpuMotion {
		glm::vec3 angular_velocity {0.0f};
		float age = 0.0f;
		uint32_t rest_steps = 0;
		uint32_t pad[3] {};
	};

	struct GpuSpawn {
		glm::vec3 position {0.0f};
		uint32_t slot = 0;
		glm::vec4 rotation {0.0f, 0.0f, 0.0f, 1.0f};
		glm::vec3 velocity {0.0f};
		uint32_t albedo_roughness = 0;
		glm::vec3 angular_velocity {0.0f};
		uint32_t packed = 0;
	};

	struct GpuVolume {
		glm::mat4 world_to_voxel {1.0f};
		glm::vec4 bounds {0.0f};
		uint32_t record_index = 0;
		uint32_t pad[3] {};
	};

	// just in case somebody touches this
	static_assert(sizeof(GpuParticle) == 48, "GpuParticle is mirrored by debris.slang");
	static_assert(sizeof(GpuMotion) == 32, "GpuMotion is mirrored by debris.slang");
	static_assert(sizeof(GpuSpawn) == 64, "GpuSpawn is mirrored by debris.slang");
	static_assert(sizeof(GpuVolume) == 96, "GpuVolume is mirrored by debris.slang");

	static constexpr uint32_t k_max_spawns_per_step = 4096;
	static constexpr uint32_t k_max_volumes = 512;
	static constexpr uint32_t k_voxels_per_particle = 8;

	explicit DebrisSimulation(uint32_t capacity);
	~DebrisSimulation() override;

	DebrisSimulation(const DebrisSimulation&) = delete;
	auto operator=(const DebrisSimulation&) -> DebrisSimulation& = delete;
	DebrisSimulation(DebrisSimulation&&) = delete;
	auto operator=(DebrisSimulation&&) -> DebrisSimulation& = delete;

	[[nodiscard]]
	static auto get() -> DebrisSimulation*;
	void spawn(std::span<const DebrisSpawn> spawns);
	void step(uint32_t steps, double fixed_delta, std::shared_ptr<const renderer::VoxelCollisionWorld> world);
	void setDisplayAlpha(float alpha, double fixed_delta);
	void clear();

	[[nodiscard]]
	auto capacity() const -> uint32_t {
		return m_capacity;
	}

	[[nodiscard]]
	auto visible() const -> bool;

	[[nodiscard]]
	auto extrapolation(uint64_t step) const -> float;    // render thread

	[[nodiscard]]
	auto frameDelta() const -> float;                    // render thread

	[[nodiscard]]
	auto particlesId() const -> BufferId {
		return m_particles;
	}

	[[nodiscard]]
	auto drawListId() const -> BufferId {
		return m_draw_list;
	}

	[[nodiscard]]
	auto drawArgsId() const -> BufferId {
		return m_draw_args;
	}

private:
	struct Params {
		glm::vec3 gravity {0.0f};
		float dt = 0.0f;
		float lifetime = 0.0f;
		float restitution = 0.0f;
		float friction = 0.0f;
		float voxel_size = 0.0f;
		uint32_t capacity = 0;
		uint32_t spawn_count = 0;
		uint32_t volume_count = 0;
		uint32_t substeps = 1;
		uint32_t clear = 0;
		uint32_t pad[3] {};
	};

	static_assert(sizeof(Params) == 64, "Params is mirrored by debris.slang");

	static constexpr uint64_t k_step_history = 16;

	uint32_t m_capacity = 0;
	uint32_t m_cursor = 0;

	BufferId m_particles = 0;
	BufferId m_motion = 0;
	BufferId m_draw_list = 0;
	BufferId m_draw_args = 0;
	BufferId m_spawns = 0;
	BufferId m_volumes = 0;

	std::deque<GpuSpawn> m_pending;
	bool m_clear_pending = false;

	std::shared_ptr<const renderer::VoxelCollisionWorld> m_world;

	mutable std::mutex m_time_mutex;
	double m_sim_time = 0.0;
	double m_display_time = 0.0;
	double m_frame_delta = 0.0;
	std::array<double, k_step_history> m_step_times {};
	bool m_visible = false;
};

}
