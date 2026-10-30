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
#include <optional>
#include <span>
#include <toast/renderer/compute/async_compute_task.hpp>
#include <utility>
#include <vector>

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
		/// Base scale times the spawn and death fades
		float scale = 0.0f;
		/// Alpha and ior byte << 8 and only read with k_flag_glass in packed
		uint32_t glass = 0;
		std::array<uint32_t, 2> pad {};
	};

	struct GpuMotion {
		glm::vec3 angular_velocity {0.0f};
		float age = 0.0f;
		uint32_t rest_steps = 0;
		float lifetime = 0.0f;
		float base_scale = 1.0f;
		uint32_t pad = 0;
	};

	struct GpuSpawn {
		glm::vec3 position {0.0f};
		uint32_t slot = 0;
		glm::vec4 rotation {0.0f, 0.0f, 0.0f, 1.0f};
		glm::vec3 velocity {0.0f};
		uint32_t albedo_roughness = 0;
		glm::vec3 angular_velocity {0.0f};
		uint32_t packed = 0;
		float lifetime = 0.0f;
		float scale = 1.0f;
		uint32_t glass = 0;
		uint32_t pad = 0;
	};

	struct GpuVolume {
		glm::mat4 world_to_voxel {1.0f};
		glm::vec4 bounds {0.0f};
		uint32_t record_index = 0;
		std::array<uint32_t, 3> pad {};
	};

	// just in case somebody touches this
	static_assert(sizeof(GpuParticle) == 64, "GpuParticle is mirrored by debris.slang and debris_draw.slang");
	static_assert(sizeof(GpuMotion) == 32, "GpuMotion is mirrored by debris.slang");
	static_assert(sizeof(GpuSpawn) == 80, "GpuSpawn is mirrored by debris.slang");
	static_assert(sizeof(GpuVolume) == 96, "GpuVolume is mirrored by debris.slang");

	static constexpr uint32_t k_max_spawns_per_step = 4096;
	static constexpr uint32_t k_max_volumes = 512;
	static constexpr uint32_t k_voxels_per_particle = 8;
	/// Mirrors debris.slang where bits 0 to 7 are the voxel mask
	static constexpr uint32_t k_flag_glass = 1u << 25;
	static constexpr float k_fade_in_seconds = 0.12f;
	static constexpr float k_fade_out_seconds = 0.6f;
	/// Slots past the cap so pieces shrinking away still have room
	static constexpr uint32_t k_cap_headroom_divisor = 4;

	explicit DebrisSimulation(uint32_t max_particles);
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
		float fade_in = 0.0f;
		float restitution = 0.0f;
		float friction = 0.0f;
		float voxel_size = 0.0f;
		uint32_t capacity = 0;
		uint32_t spawn_count = 0;
		uint32_t volume_count = 0;
		uint32_t substeps = 1;
		uint32_t clear = 0;
		uint32_t retire_count = 0;
		float fade_out = 0.0f;
		uint32_t pad = 0;
	};

	static_assert(sizeof(Params) == 64, "Params is mirrored by debris.slang");

	static constexpr uint64_t k_step_history = 16;

	/// Picks spawn slots and retires the oldest past the cap
	/// Owner thread inside a step that reached the GPU
	auto planSlots(double now, std::span<GpuSpawn> spawns, std::span<uint32_t> retires) -> uint32_t;
	auto findFreeSlot(double now) -> std::optional<uint32_t>;
	auto randomUnit() -> float;

	uint32_t m_capacity = 0;
	uint32_t m_max_particles = 0;
	uint32_t m_cursor = 0;

	/// Sim time each slot dies at and whether it was already told to shrink away
	std::vector<double> m_slot_death;
	std::vector<uint8_t> m_slot_retiring;
	std::vector<uint64_t> m_slot_sequence;
	/// Slot and spawn sequence oldest first and stale once the slot dies or is reused
	std::deque<std::pair<uint32_t, uint64_t>> m_spawn_order;
	uint64_t m_next_sequence = 0;
	uint64_t m_rng = 0x9e3779b97f4a7c15ull;

	BufferId m_particles = 0;
	BufferId m_motion = 0;
	BufferId m_draw_list = 0;
	BufferId m_draw_args = 0;
	BufferId m_spawns = 0;
	BufferId m_volumes = 0;
	BufferId m_retires = 0;

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
