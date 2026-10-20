#include "debris_simulation.hpp"

#include "physics_settings.hpp"
#include "simulator.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <toast/log.hpp>
#include <toast/renderer/voxel_gpu_storage.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/voxel_constants.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace physics {

namespace {

std::atomic<DebrisSimulation*> g_debris_simulation {nullptr};

constexpr uint32_t k_debris_group_size = 64;
constexpr uint32_t k_debris_max_substeps = 4;
constexpr uint32_t k_debris_reset_entry = 0;
constexpr uint32_t k_debris_integrate_entry = 1;
constexpr uint32_t k_debris_spawn_entry = 2;
constexpr double k_debris_min_extrapolation = -0.1;
constexpr double k_debris_max_extrapolation = 0.25;

[[nodiscard]]
auto debrisGroups(uint32_t count) -> uint32_t {
	return (count + k_debris_group_size - 1) / k_debris_group_size;
}

}

DebrisSimulation::DebrisSimulation(uint32_t capacity)
    : AsyncComputeTask({
        .shader_uri = "core://shaders/debris.slang",
        .entries = {"resetMain", "integrateMain", "spawnMain"},
        .debug_name = "Debris"
}),
      m_capacity(std::max(capacity, 1u)) {
	ZoneScoped;

	// motion is always on this shader
	m_particles = addRingBuffer(sizeof(GpuParticle) * m_capacity);
	m_motion = addPersistentBuffer(sizeof(GpuMotion) * m_capacity);
	m_draw_list = addRingBuffer(sizeof(uint32_t) * m_capacity * k_voxels_per_particle);
	m_draw_args = addRingBuffer(sizeof(uint32_t) * 4);
	m_spawns = addHostBuffer(sizeof(GpuSpawn) * k_max_spawns_per_step);
	m_volumes = addHostBuffer(sizeof(GpuVolume) * k_max_volumes);
	setAcquireUsage(m_draw_args, renderer::sync::Usage::indirect_read);

	bind(0, m_particles, Copy::previous);
	bind(1, m_particles, Copy::current);
	bind(2, m_motion);
	bind(3, m_draw_list);
	bind(4, m_draw_args);
	bind(5, m_spawns);
	bind(6, m_volumes);

	const auto section = [this](renderer::VoxelGpuStorage::Section which) {
		return [this, which]() -> vk::Buffer {
			return m_world != nullptr && m_world->storage != nullptr ? m_world->storage->buffer(which) : vk::Buffer {};
		};
	};
	bindExternal(1, 0, section(renderer::VoxelGpuStorage::Section::materials));
	bindExternal(1, 2, section(renderer::VoxelGpuStorage::Section::grids));
	bindExternal(1, 5, section(renderer::VoxelGpuStorage::Section::records));

	if (isReady()) {
		// first step reads the previous copy
		const std::vector<GpuParticle> particles(m_capacity);
		const std::vector<GpuMotion> motion(m_capacity);
		upload(m_particles, std::span<const GpuParticle>(particles));
		upload(m_motion, std::span<const GpuMotion>(motion));
	} else {
		TOAST_ERROR("Physics", "Debris simulation unavailable, debris will be dropped");
	}

	g_debris_simulation.store(this, std::memory_order_release);
}

DebrisSimulation::~DebrisSimulation() {
	DebrisSimulation* expected = this;
	g_debris_simulation.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

auto DebrisSimulation::get() -> DebrisSimulation* {
	return g_debris_simulation.load(std::memory_order_acquire);
}

void DebrisSimulation::spawn(std::span<const DebrisSpawn> spawns) {
	for (const DebrisSpawn& spawn : spawns) {
		if (spawn.mask == 0) {
			continue;
		}
		m_pending.push_back(
		    GpuSpawn {
		      .position = spawn.position,
		      .slot = m_cursor,
		      .rotation = {spawn.rotation.x, spawn.rotation.y, spawn.rotation.z, spawn.rotation.w},
		      .velocity = spawn.velocity,
		      .albedo_roughness = spawn.albedo_roughness,
		      .angular_velocity = spawn.angular_velocity,
		      .packed = static_cast<uint32_t>(spawn.mask) | (static_cast<uint32_t>(spawn.metallic) << 8u) |
					          (static_cast<uint32_t>(spawn.reflectivity) << 16u),
		}
		);
		m_cursor = (m_cursor + 1) % m_capacity;
	}
	// Anything older would be overwritten before drawn
	while (m_pending.size() > m_capacity) {
		m_pending.pop_front();
	}
}

void DebrisSimulation::step(uint32_t steps, double fixed_delta, std::shared_ptr<const renderer::VoxelCollisionWorld> world) {
	ZoneScopedN("physics::DebrisStep");
	if (steps == 0 || !isReady()) {
		m_pending.clear();
		return;
	}

	const auto spawn_count = static_cast<uint32_t>(std::min<size_t>(m_pending.size(), k_max_spawns_per_step));
	const auto volume_count = world != nullptr ? static_cast<uint32_t>(std::min<size_t>(world->volumes.size(), k_max_volumes)) : 0u;
	const double total = static_cast<double>(steps) * fixed_delta;
	const uint32_t substeps = std::min(steps, k_debris_max_substeps);

	const Tunables& settings = tunables();
	const Params params {
	  .gravity = settings.gravity,
	  .dt = static_cast<float>(total / static_cast<double>(substeps)),
	  .lifetime = settings.debris_lifetime,
	  .restitution = settings.debris_restitution,
	  .friction = settings.debris_friction,
	  .voxel_size = voxel::k_voxel_size,
	  .capacity = m_capacity,
	  .spawn_count = spawn_count,
	  .volume_count = volume_count,
	  .substeps = substeps,
	  .clear = m_clear_pending ? 1u : 0u,
	};

	m_world = world;
	const uint64_t step_id = AsyncComputeTask::step(
	    [&](StepContext& ctx) {
		    const std::span<GpuSpawn> spawns = ctx.hostData<GpuSpawn>(m_spawns);
		    std::copy_n(m_pending.begin(), std::min<size_t>(spawn_count, spawns.size()), spawns.begin());

		    const std::span<GpuVolume> volumes = ctx.hostData<GpuVolume>(m_volumes);
		    for (uint32_t i = 0; i < volume_count && i < volumes.size(); ++i) {
			    const renderer::VoxelCollisionWorld::Volume& volume = world->volumes[i];
			    volumes[i] = GpuVolume {
			      .world_to_voxel = volume.world_to_voxel,
			      .bounds = glm::vec4(volume.bounds_center, volume.bounds_radius),
			      .record_index = volume.record_index,
			    };
		    }

		    ctx.dispatch(1, params, k_debris_reset_entry);
		    ctx.dispatch(debrisGroups(m_capacity), params, k_debris_integrate_entry);
		    if (spawn_count > 0) {
			    ctx.dispatch(debrisGroups(spawn_count), params, k_debris_spawn_entry);
		    }
	    },
	    "Debris",
	    world
	);
	m_world.reset();

	if (step_id == 0) {
		return;
	}
	m_pending.erase(m_pending.begin(), m_pending.begin() + spawn_count);
	m_clear_pending = false;

	std::scoped_lock lock(m_time_mutex);
	m_sim_time += total;
	m_step_times[step_id % k_step_history] = m_sim_time;
	m_visible = m_visible || spawn_count > 0;
}

void DebrisSimulation::setDisplayAlpha(float alpha, double fixed_delta) {
	std::scoped_lock lock(m_time_mutex);
	const double display = m_sim_time - ((1.0 - static_cast<double>(std::clamp(alpha, 0.0f, 1.0f))) * fixed_delta);
	m_frame_delta = std::clamp(display - m_display_time, 0.0, k_debris_max_extrapolation);
	m_display_time = display;
}

void DebrisSimulation::clear() {
	m_pending.clear();
	m_clear_pending = true;
	std::scoped_lock lock(m_time_mutex);
	m_visible = false;
}

auto DebrisSimulation::visible() const -> bool {
	std::scoped_lock lock(m_time_mutex);
	return m_visible;
}

auto DebrisSimulation::extrapolation(uint64_t step) const -> float {
	if (step == 0) {
		return 0.0f;
	}
	std::scoped_lock lock(m_time_mutex);
	const double behind = m_display_time - m_step_times[step % k_step_history];
	return static_cast<float>(std::clamp(behind, k_debris_min_extrapolation, k_debris_max_extrapolation));
}

auto DebrisSimulation::frameDelta() const -> float {
	std::scoped_lock lock(m_time_mutex);
	return static_cast<float>(m_frame_delta);
}

void stepDebris(size_t steps) {
	ZoneScopedN("physics::StepDebris");
	Simulator* simulator = Simulator::current();
	const std::vector<DebrisSpawn> spawns = simulator != nullptr ? simulator->takeDebrisSpawns() : std::vector<DebrisSpawn> {};

	DebrisSimulation* debris = DebrisSimulation::get();
	if (debris == nullptr) {
		return;
	}
	debris->spawn(spawns);

	const double fixed_delta = tunables().fixedDelta();
	if (steps > 0) {
		const renderer::VulkanRenderer* renderer = renderer::VulkanRenderer::instance;
		debris->step(static_cast<uint32_t>(steps), fixed_delta, renderer != nullptr ? renderer->voxelCollisionWorld() : nullptr);
	}
	debris->setDisplayAlpha(Simulator::interpolationAlpha(), fixed_delta);
}

void clearDebris() {
	if (Simulator* simulator = Simulator::current(); simulator != nullptr) {
		auto discarded = simulator->takeDebrisSpawns();
	}
	if (DebrisSimulation* debris = DebrisSimulation::get(); debris != nullptr) {
		debris->clear();
	}
}

}
