/**
 * @file physics_settings.hpp
 * @author Xein
 * @date 22 Sep 2026
 */

#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <toast/export.hpp>
#include <toast/params/group.hpp>
#include <vector>

namespace physics {

[[nodiscard]]
TOAST_API auto defaultLayerNames() -> std::vector<std::string>;

[[nodiscard]]
TOAST_API auto defaultLayerAbbreviations() -> std::vector<std::string>;

struct PhysicsParams : toast::params::Group<"physics"> {
	template<typename T>
	using Param = toast::params::Param<T>;
	using Kind = toast::params::Kind;

	Param<glm::vec3> gravity {
	  "world.gravity",
	  {0.0f, 0.0f, -9.8f},
	  {.name = "Gravity",
		  .min = glm::vec3 {-100.0f},
		  .max = glm::vec3 {100.0f},
		  .unit = "m/s²",
		  .description = "Pull on every body"}
	};
	Param<float> air_density {
	  "world.air_density",
	  1.225f,
	  {.name = "Air density", .min = 0.0f, .unit = "kg/m³", .description = "kg/m³. More makes drag stronger"}
	};
	Param<glm::vec3> wind {
	  "world.wind",
	  {0.0f, 0.0f, 0.0f},
	  {.name = "Wind", .min = glm::vec3 {-1000.0f}, .max = glm::vec3 {1000.0f}, .unit = "m/s", .description = "Wind speed"}
	};
	Param<float> wind_shelter_distance {
	  "world.wind_shelter_distance",
	  64.0f,
	  {.name = "Wind shelter distance",
		  .min = 0.0f,
		  .unit = "m",
		  .description = "Solid geometry this far upwind blocks the wind. 0 turns it off"}
	};
	Param<float> kill_plane_z {
	  "world.kill_plane_z",
	  -50.0f,
	  {.name = "Kill plane Z",
		       .min = -100000.0f,
		       .max = 100000.0f,
		       .unit = "m",
		       .description = "Bodies that fall below this height are destroyed"}
	};
	Param<uint32_t> solver_iterations {
	  "solver.iterations",
	  8,
	  {.name = "Iterations", .min = 1, .max = 32, .description = "Passes per step. More is stiffer and slower"}
	};
	Param<uint32_t> position_iterations {
	  "solver.position_iterations",
	  3,
	  {.name = "Position iterations",
		  .min = 1,
		  .max = 16,
		  .kind = Kind::advanced,
		  .description = "Extra passes that push overlaps apart"}
	};
	Param<uint32_t> joint_substeps {
	  "solver.joint_substeps",
	  4,
	  {.name = "Joint substeps",
		  .min = 1,
		  .max = 16,
		  .kind = Kind::advanced,
		  .description = "Splits each step for bodies on joints so swings keep their speed"}
	};
	Param<float> penetration_slop {
	  "solver.penetration_slop",
	  0.005f,
	  {.name = "Penetration slop", .min = 0.0f, .max = 0.1f, .unit = "m", .description = "Overlap left alone"}
	};
	Param<float> correction_beta {
	  "solver.correction_beta",
	  0.2f,
	  {.name = "Correction beta",
		  .min = 0.0f,
		  .max = 1.0f,
		  .kind = Kind::advanced,
		  .description = "Share of an overlap fixed each step"}
	};
	Param<float> max_correction {
	  "solver.max_correction",
	  0.05f,
	  {.name = "Max correction", .min = 0.0f, .max = 1.0f, .unit = "m", .description = "Most an overlap is pushed in one step"}
	};
	Param<float> bounce_threshold {
	  "solver.bounce_threshold",
	  1.0f,
	  {.name = "Bounce threshold",
		  .min = 0.0f,
		  .max = 10.0f,
		  .unit = "m/s",
		  .description = "Hits slower than this (m/s) do not bounce"}
	};
	Param<float> sleep_linear_threshold {
	  "sleep.linear_threshold",
	  0.05f,
	  {.name = "Linear threshold",
		  .min = 0.0f,
		  .max = 1.0f,
		  .unit = "m/s",
		  .description = "Slower than this (m/s), a body can fall asleep"}
	};
	Param<float> sleep_angular_threshold {
	  "sleep.angular_threshold",
	  0.05f,
	  {.name = "Angular threshold",
		  .min = 0.0f,
		  .max = 1.0f,
		  .unit = "rad/s",
		  .description = "Spinning slower than this (rad/s), a body can fall asleep"}
	};
	Param<float> sleep_delay {
	  "sleep.delay",
	  0.5f,
	  {.name = "Delay", .min = 0.0f, .max = 10.0f, .unit = "s", .description = "Seconds a body stays still before it sleeps"}
	};
	Param<float> sleep_drift {
	  "sleep.drift",
	  0.05f,
	  {.name = "Drift", .min = 0.005f, .max = 0.5f, .unit = "m", .description = "How far a body may wander in that time"}
	};
	Param<double> frequency {
	  "step.frequency",
	  60.0,
	  {.name = "Frequency", .min = 15.0, .max = 240.0, .unit = "Hz", .description = "Physics steps per second"}
	};
	Param<uint32_t> max_substeps {
	  "step.max_substeps", 8, {.name = "Max substeps", .min = 1, .max = 32, .description = "Most steps one frame may catch up with"}
	};
	Param<double> max_burst_seconds {
	  "step.max_burst_seconds",
	  0.1,
	  {.name = "Max burst seconds",
		  .min = 0.0,
		  .max = 1.0,
		  .unit = "s",
		  .kind = Kind::advanced,
		  .description = "Most real time a catch-up may take after its first step"}
	};
	Param<float> broadphase_fat_margin {
	  "broadphase.fat_margin",
	  0.1f,
	  {.name = "Fat margin",
		  .min = 0.0f,
		  .max = 1.0f,
		  .unit = "m",
		  .kind = Kind::advanced,
		  .description = "Padding around each shape's bounds"}
	};
	Param<float> broadphase_velocity_multiplier {
	  "broadphase.velocity_multiplier",
	  2.0f,
	  {.name = "Velocity multiplier",
		  .min = 0.0f,
		  .max = 8.0f,
		  .kind = Kind::advanced,
		  .description = "How far bounds stretch ahead of a moving shape, in steps"}
	};
	Param<float> broadphase_rebuild_fraction {
	  "broadphase.rebuild_fraction",
	  0.25f,
	  {.name = "Rebuild fraction",
		  .min = 0.0f,
		  .max = 2.0f,
		  .kind = Kind::advanced,
		  .description = "Share of shapes that may move before the tree is rebuilt"}
	};
	Param<uint32_t> max_connectivity_jobs_per_tick {
	  "fracture.max_connectivity_jobs_per_tick",
	  24,
	  {.name = "Max connectivity jobs per tick",
		  .min = 1,
		  .max = 256,
		  .kind = Kind::advanced,
		  .description = "Pieces checked for being cut off, per tick"}
	};
	Param<uint32_t> max_fragment_spawns_per_step {
	  "fracture.max_spawns_per_step",
	  8,
	  {.name = "Max fragment spawns per step", .min = 0, .max = 64, .description = "Fragments that become bodies per step"}
	};
	Param<uint32_t> max_active_fragments {
	  "fracture.max_active_fragments",
	  96,
	  {.name = "Max active fragments",
		  .min = 0,
		  .max = 16384,
		  .description = "Awake fragments allowed before settled ones are put to sleep"}
	};
	Param<uint32_t> fragment_pool_headroom {
	  "fracture.pool_headroom",
	  4096,
	  {.name = "Fragment pool headroom",
		  .min = 0,
		  .max = 65536,
		  .kind = Kind::advanced,
		  .description = "Spare bricks kept for fragments"}
	};
	Param<uint32_t> min_fragment_voxels {
	  "fracture.min_fragment_voxels",
	  4,
	  {.name = "Min fragment voxels",
		  .min = 1,
		  .max = 256,
		  .unit = "voxels",
		  .description = "Smaller pieces are discarded instead of becoming debris"}
	};
	Param<uint32_t> fragment_despawn_max_voxels {
	  "fracture.despawn_max_voxels",
	  16,
	  {.name = "Despawn max voxels",
		  .min = 0,
		  .max = 1024,
		  .unit = "voxels",
		  .description = "Largest fragment that may despawn on its own"}
	};
	Param<uint32_t> large_debris_min_voxels {
	  "fracture.large_debris_min_voxels",
	  16,
	  {.name = "Large debris min voxels",
		  .min = 1,
		  .max = 4096,
		  .unit = "voxels",
		  .description = "From this many voxels a fragment goes on the large debris layer"}
	};
	Param<float> fragment_despawn_settle_seconds {
	  "fracture.despawn_settle_seconds",
	  3.0f,
	  {.name = "Despawn settle time",
		  .min = 0.0f,
		  .max = 60.0f,
		  .unit = "s",
		  .description = "Seconds a small fragment sleeps before it despawns"}
	};
	Param<float> force_sleep_slack {
	  "fracture.force_sleep_slack",
	  4.0f,
	  {.name = "Force sleep slack",
		  .min = 1.0f,
		  .max = 64.0f,
		  .kind = Kind::advanced,
		  .description = "Sleep limits are this much looser while over the fragment budget"}
	};
	Param<float> fragment_sleep_scale {
	  "fracture.sleep_scale",
	  3.0f,
	  {.name = "Fragment sleep scale",
		  .min = 1.0f,
		  .max = 20.0f,
		  .description = "Fragments sleep with the limits scaled by this, so debris settles sooner"}
	};
	Param<int32_t> max_fragment_extent_bricks {
	  "fracture.max_fragment_extent_bricks",
	  1,
	  {.name = "Max fragment extent (bricks)",
		  .min = 1,
		  .max = 16,
		  .unit = "bricks",
		  .kind = Kind::advanced,
		  .description = "Largest piece, in bricks along any axis, that still counts as a fragment"}
	};
	Param<float> fracture_shell_voxels {
	  "fracture.shell_voxels",
	  1.0f,
	  {.name = "Fracture shell voxels",
		  .min = 0.0f,
		  .max = 16.0f,
		  .unit = "voxels",
		  .description = "Thickness of the shell carved around a hit"}
	};
	Param<float> fracture_depth_share {
	  "fracture.depth_share",
	  0.5f,
	  {.name = "Depth share", .min = 0.0f, .max = 1.0f, .description = "Energy that carries on after a hit breaks through"}
	};
	Param<float> fracture_max_depth {
	  "fracture.max_depth",
	  2.0f,
	  {.name = "Max depth", .min = 0.0f, .max = 16.0f, .unit = "m", .description = "Most a hit digs past its radius"}
	};
	Param<float> fracture_max_overshoot {
	  "fracture.max_overshoot",
	  2.0f,
	  {.name = "Max overshoot", .min = 0.0f, .max = 16.0f, .description = "Most a hit widens past its radius"}
	};
	Param<float> explosion_max_radius {
	  "fracture.explosion_max_radius",
	  8.0f,
	  {.name = "Explosion max radius",
		  .min = 0.0f,
		  .max = 64.0f,
		  .unit = "m",
		  .description = "Reach of an explosion that sets no radius of its own"}
	};
	Param<float> explosion_falloff {
	  "fracture.explosion_falloff",
	  2.0f,
	  {.name = "Explosion falloff",
		  .min = 0.5f,
		  .max = 8.0f,
		  .description = "How fast force fades with distance. 2 is inverse square"}
	};
	Param<float> explosion_chunk_size {
	  "fracture.explosion_chunk_size",
	  1.0f,
	  {.name = "Explosion chunk size",
		  .min = 0.2f,
		  .max = 4.0f,
		  .unit = "m",
		  .description = "Size of the chunks a blast breaks things into"}
	};
	Param<float> explosion_debris_ratio {
	  "fracture.explosion_debris_ratio",
	  8.0f,
	  {.name = "Explosion debris ratio",
		  .min = 1.0f,
		  .max = 64.0f,
		  .description = "A blast this many times tougher than the material turns it to debris"}
	};
	Param<float> explosion_shatter_width {
	  "fracture.explosion_shatter_width",
	  0.6f,
	  {.name = "Explosion shatter width",
		  .min = 0.0f,
		  .max = 4.0f,
		  .unit = "m",
		  .description = "Cracked material left around the hole. 0 for none"}
	};
	Param<float> explosion_max_speed {
	  "fracture.explosion_max_speed",
	  20.0f,
	  {.name = "Explosion max speed",
		  .min = 1.0f,
		  .max = 100.0f,
		  .unit = "m/s",
		  .description = "Fastest a blast throws anything. Too fast tunnels through thin colliders"}
	};
	Param<uint32_t> explosion_max_fragments {
	  "fracture.explosion_max_fragments",
	  48,
	  {.name = "Explosion max fragments",
		  .min = 0,
		  .max = 256,
		  .description = "Chunks per shape that become bodies. The rest turn to debris"}
	};
	Param<uint32_t> impact_max_hardness {
	  "fracture.impact_max_hardness",
	  1,
	  {.name = "Impact max hardness", .min = 0, .max = 2, .description = "Hardest material a collision can break"}
	};
	Param<float> fracture_bond_cost {
	  "fracture.bond_cost", 1.0f, {.name = "Bond cost", .min = 0.0f, .max = 16.0f, .kind = Kind::advanced}
	};
	Param<uint32_t> fracture_max_unglue_voxels {
	  "fracture.max_unglue_voxels", 4096, {.name = "Max unglue voxels", .min = 0, .max = 1000000, .kind = Kind::advanced}
	};
	Param<uint32_t> fracture_max_shatter_cells {
	  "fracture.max_shatter_cells", 128, {.name = "Max shatter cells", .min = 2, .max = 1024, .kind = Kind::advanced}
	};
	Param<float> fracture_max_shatter_distance {
	  "fracture.max_shatter_distance", 8.0f, {.name = "Max shatter distance", .min = 0.1f, .max = 64.0f, .kind = Kind::advanced}
	};
	Param<uint32_t> debris_max_spawns_per_step {
	  "debris.max_spawns_per_step",
	  512,
	  {.name = "Max spawns per step", .min = 0, .max = 16384, .description = "Debris pieces created per step"}
	};
	Param<float> debris_speed_scale {
	  "debris.speed_scale",
	  1.0f,
	  {.name = "Speed scale", .min = 0.0f, .max = 8.0f, .description = "Scales how fast debris is thrown"}
	};
	Param<float> debris_density_multiplier {
	  "debris.density_multiplier",
	  1.0f,
	  {.name = "Density multiplier", .min = 0.1f, .max = 100.0f, .description = "Heavier debris is thrown slower"}
	};
	Param<float> debris_max_speed {
	  "debris.max_speed",
	  12.0f,
	  {.name = "Max speed", .min = 0.5f, .max = 40.0f, .unit = "m/s", .description = "Fastest a piece is thrown"}
	};
	Param<float> debris_max_spin {
	  "debris.max_spin",
	  20.0f,
	  {.name = "Max spin", .min = 1.0f, .max = 60.0f, .unit = "rad/s", .description = "Fastest a piece spins, in rad/s"}
	};
	Param<float> debris_lifetime {
	  "debris.lifetime",
	  12.0f,
	  {.name = "Lifetime", .min = 0.5f, .max = 600.0f, .unit = "s", .description = "Seconds a piece lasts"}
	};
	Param<float> debris_lifetime_jitter {
	  "debris.lifetime_jitter",
	  0.3f,
	  {.name = "Lifetime jitter",
		  .min = 0.0f,
		  .max = 0.9f,
		  .description = "Random spread on the lifetime, so a burst does not vanish at once"}
	};
	Param<uint32_t> debris_max_particles {
	  "debris.max_particles",
	  16384,
	  {.name = "Max particles",
		  .min = 256,
		  .max = 262144,
		  .description = "Past this the oldest pieces shrink away",
		  .requires_restart = true}
	};
	Param<float> debris_restitution {
	  "debris.restitution", 0.3f, {.name = "Restitution", .min = 0.0f, .max = 1.0f}
	};
	Param<float> debris_friction {
	  "debris.friction", 0.6f, {.name = "Friction", .min = 0.0f, .max = 1.0f}
	};
	Param<uint32_t> min_bounds_per_job {
	  "jobs.min_bounds_per_job",
	  32,
	  {.name = "Min bounds per job",
		  .min = 1,
		  .max = 1024,
		  .kind = Kind::advanced,
		  .description = "Fewer bounds than this update on the calling thread"}
	};
	Param<uint32_t> min_candidates_per_job {
	  "jobs.min_candidates_per_job",
	  2,
	  {.name = "Min candidates per job",
		  .min = 1,
		  .max = 1024,
		  .kind = Kind::advanced,
		  .description = "Fewer candidate pairs than this are tested inline"}
	};
	Param<uint32_t> min_manifolds_per_job {
	  "jobs.min_manifolds_per_job",
	  4,
	  {.name = "Min manifolds per job",
		  .min = 1,
		  .max = 1024,
		  .kind = Kind::advanced,
		  .description = "Fewer contacts than this are prepared inline"}
	};
	Param<uint32_t> min_shapes_per_query_job {
	  "jobs.min_shapes_per_query_job",
	  16,
	  {.name = "Min shapes per query job",
		  .min = 1,
		  .max = 1024,
		  .kind = Kind::advanced,
		  .description = "Fewer shapes than this are queried inline"}
	};
	Param<uint32_t> min_bodies_per_job {
	  "jobs.min_bodies_per_job",
	  64,
	  {.name = "Min bodies per job",
		  .min = 1,
		  .max = 4096,
		  .kind = Kind::advanced,
		  .description = "Fewer bodies than this integrate inline"}
	};
	Param<uint32_t> min_wave_constraints_for_dispatch {
	  "jobs.min_wave_constraints_for_dispatch",
	  512,
	  {.name = "Min wave constraints per dispatch",
		  .min = 1,
		  .max = 65536,
		  .kind = Kind::advanced,
		  .description = "Smaller constraint waves solve inline"}
	};
	Param<uint32_t> min_wave_joints_for_dispatch {
	  "jobs.min_wave_joints_for_dispatch",
	  64,
	  {.name = "Min wave joints per dispatch",
		  .min = 1,
		  .max = 65536,
		  .kind = Kind::advanced,
		  .description = "Smaller joint waves solve inline"}
	};
	Param<uint32_t> box_voxel_split_min_voxels {
	  "jobs.box_voxel_split_min_voxels",
	  500000,
	  {.name = "Box-voxel split threshold",
		  .min = 1,
		  .max = 1000000,
		  .kind = Kind::advanced,
		  .description = "Box against voxel pairs this big are split across jobs"}
	};
	Param<uint32_t> voxel_voxel_split_min_voxels {
	  "jobs.voxel_voxel_split_min_voxels",
	  250000,
	  {.name = "Voxel-voxel split threshold",
		  .min = 1,
		  .max = 1000000,
		  .kind = Kind::advanced,
		  .description = "Voxel against voxel pairs this big are split across jobs"}
	};
	Param<uint32_t> voxel_pair_split_regions {
	  "jobs.voxel_pair_split_regions",
	  8,
	  {.name = "Voxel pair split regions",
		  .min = 1,
		  .max = 64,
		  .kind = Kind::advanced,
		  .description = "Jobs a split pair is divided into"}
	};
	Param<bool> time_narrow_pairs {
	  "step.time_narrow_pairs",
	  false,
	  {.name = "Time narrow phase pairs",
		  .kind = Kind::debug,
		  .description = "Times each kind of narrow phase pair for the overlay"}
	};

	Param<bool> draw_shapes {
	  "world.draw_shapes",
	  false,
	  {.name = "Draw shapes",
		  .kind = Kind::debug,
		  .description = "Outlines colliders: green awake, blue asleep, yellow kinematic, grey fixed"}
	};
	Param<bool> draw_bounds {
	  "broadphase.draw_bounds",
	  false,
	  {.name = "Draw bounds", .kind = Kind::debug, .description = "The box the broad phase tests first"}
	};
	Param<bool> draw_contacts {
	  "solver.draw_contacts",
	  false,
	  {.name = "Draw contacts", .kind = Kind::debug, .description = "Contact points and the normals they push along"}
	};
	Param<bool> draw_velocity {
	  "world.draw_velocity",
	  false,
	  {.name = "Draw velocity", .kind = Kind::debug, .description = "An arrow along each awake body's motion"}
	};
	Param<bool> draw_centers {
	  "world.draw_centers",
	  false,
	  {.name = "Draw centers of mass", .kind = Kind::debug, .description = "A dot at each center of mass"}
	};
	Param<float> velocity_scale {
	  "world.velocity_scale",
	  0.25f,
	  {.name = "Velocity arrow scale",
		  .min = 0.01f,
		  .max = 5.0f,
		  .unit = "s",
		  .kind = Kind::debug,
		  .description = "Seconds of travel an arrow covers"}
	};
	Param<float> contact_normal_length {
	  "solver.contact_normal_length",
	  0.3f,
	  {.name = "Contact normal length",
		  .min = 0.05f,
		  .max = 3.0f,
		  .unit = "m",
		  .kind = Kind::debug,
		  .description = "Length of a contact normal"}
	};
	Param<uint32_t> sweep_refine_iterations {
	  "character.sweep_refine_iterations",
	  6,
	  {.name = "Sweep refine iterations", .min = 1, .max = 32, .description = "Refinement steps after a sweep hits something"}
	};
	Param<std::vector<std::string>> layer_names {
	  "layers.names", defaultLayerNames(), {.name = "Layer names", .description = "Layer 0 cannot be renamed"}
	};
	Param<std::vector<std::string>> layer_abbreviations {
	  "layers.abbreviations", defaultLayerAbbreviations(), {.name = "Layer abbreviations", .description = "Up to three characters"}
	};

	[[nodiscard]]
	auto fixedDelta() const noexcept -> double {
		return frequency.get() > 0.0 ? 1.0 / frequency.get() : 1.0 / 60.0;
	}
};

using Tunables = PhysicsParams;

[[nodiscard]]
TOAST_API auto tunables() -> const Tunables&;

TOAST_API void registerPhysicsSettings();

}
