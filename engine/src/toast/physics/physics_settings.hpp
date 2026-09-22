/**
 * @file physics_settings.hpp
 * @author Xein
 * @date 22 Sep 2026
 */

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <toast/export.hpp>

namespace physics {

struct Tunables {
	glm::vec3 gravity {0.0f, 0.0f, -9.8f};
	uint32_t solver_iterations = 8;
	float penetration_slop = 0.005f;
	float correction_beta = 0.2f;
	float max_correction = 0.05f;
	float bounce_threshold = 1.0f;
	float sleep_linear_threshold = 0.05f;
	float sleep_angular_threshold = 0.05f;
	float sleep_delay = 0.5f;
	double frequency = 60.0;
	uint32_t max_substeps = 8;
	double max_burst_seconds = 0.1;
	float broadphase_fat_margin = 0.1f;
	uint32_t max_connectivity_jobs_per_tick = 24;
	uint32_t max_fragment_spawns_per_step = 8;
	uint32_t max_active_fragments = 96;
	uint32_t fragment_pool_headroom = 4096;
	uint32_t min_fragment_voxels = 4;
	uint32_t fragment_despawn_max_voxels = 16;
	float fragment_despawn_settle_seconds = 3.0f;
	float force_sleep_slack = 4.0f;
	int32_t max_fragment_extent_bricks = 1;
	float fracture_shell_voxels = 2.0f;
	uint32_t min_bounds_per_job = 32;
	uint32_t min_candidates_per_job = 2;
	uint32_t min_wave_constraints_for_dispatch = 512;

	[[nodiscard]]
	auto fixedDelta() const noexcept -> double {
		return frequency > 0.0 ? 1.0 / frequency : 1.0 / 60.0;
	}
};

[[nodiscard]]
TOAST_API auto tunables() -> const Tunables&;

TOAST_API void registerPhysicsSettings();

}
