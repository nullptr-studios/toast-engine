#include "physics_settings.hpp"

#include "layers.hpp"

#include <algorithm>
#include <string>
#include <toast/settings/settings.hpp>

namespace physics {
namespace {

using toast::settings::Meta;

Tunables g_tunables;

}

auto tunables() -> const Tunables& {
	return g_tunables;
}

void registerPhysicsSettings() {
	registerLayerSettings();

	const auto flt = [](std::string_view key, double value, Meta meta, float Tunables::* field) {
		toast::settings::declareFloat(key, value, std::move(meta)).onChange([field](double v) {
			g_tunables.*field = static_cast<float>(v);
		});
	};
	const auto uint_setting = [](std::string_view key, int64_t value, Meta meta, uint32_t Tunables::* field) {
		toast::settings::declareInt(key, value, std::move(meta)).onChange([field](int64_t v) {
			g_tunables.*field = static_cast<uint32_t>(std::max<int64_t>(v, 0));
		});
	};

	const auto gravity_axis = [](std::string_view key, std::string_view label, double value, int axis) {
		toast::settings::declareFloat(
		    key,
		    value,
		    {.label = std::string {label},
				 .category = "World",
				 .description = "Acceleration applied to every dynamic body, scaled per body by its gravity scale.",
				 .min = -100.0,
				 .max = 100.0,
				 .step = 0.1}
		)
		    .onChange([axis](double v) { g_tunables.gravity[axis] = static_cast<float>(v); });
	};
	gravity_axis("physics.world.gravity_x", "Gravity X", 0.0, 0);
	gravity_axis("physics.world.gravity_y", "Gravity Y", 0.0, 1);
	gravity_axis("physics.world.gravity_z", "Gravity Z", -9.8, 2);

	flt("physics.world.air_density",
	    1.225f,
	    {.label = "Air density", .category = "World", .min = 0.0f, .step = 0.1f},
	    &Tunables::air_density);

	const auto wind_axis = [](std::string_view key, std::string_view label, double value, int axis) {
		toast::settings::declareFloat(
		    key, value, {.label = std::string {label}, .category = "World", .min = -1000.0, .max = 1000.0, .step = 0.1}
		)
		    .onChange([axis](double v) { g_tunables.wind[axis] = static_cast<float>(v); });
	};
	wind_axis("physics.world.wind_x", "Wind X", 0.0f, 0);
	wind_axis("physics.world.wind_y", "Wind Y", 0.0f, 1);
	wind_axis("physics.world.wind_z", "Wind Z", 0.0f, 2);

	flt("physics.world.wind_shelter_distance",
	    64.0f,
	    {.label = "Wind shelter distance",
	     .category = "World",
	     .description = "A body with solid geometry this far upwind of it, like the wall of a building, feels no wind. 0 disables.",
	     .min = 0.0f,
	     .step = 1.0f},
	    &Tunables::wind_shelter_distance);

	uint_setting(
	    "physics.solver.iterations",
	    8,
	    {.label = "Iterations",
			 .category = "Solver",
			 .description = "Sequential impulse passes per step",
			 .min = 1.0,
			 .max = 32.0,
			 .step = 1.0},
	    &Tunables::solver_iterations
	);
	uint_setting(
	    "physics.solver.position_iterations",
	    3,
	    {.label = "Position iterations",
			 .category = "Solver",
			 .description = "Passes that push overlapping bodies apart after the velocity solve",
			 .min = 1.0,
			 .max = 16.0,
			 .step = 1.0},
	    &Tunables::position_iterations
	);
	flt("physics.solver.penetration_slop",
	    0.005,
	    {.label = "Penetration slop",
	     .category = "Solver",
	     .description = "Overlap left uncorrected, in metres",
	     .min = 0.0,
	     .max = 0.1,
	     .step = 0.001},
	    &Tunables::penetration_slop);
	flt("physics.solver.correction_beta",
	    0.2,
	    {.label = "Correction beta",
	     .category = "Solver",
	     .description = "The fraction of the remaining overlap resolved each step",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.01},
	    &Tunables::correction_beta);
	flt("physics.solver.max_correction",
	    0.05,
	    {.label = "Max correction",
	     .category = "Solver",
	     .description = "Ceiling on one step's positional correction",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.005},
	    &Tunables::max_correction);
	flt("physics.solver.bounce_threshold",
	    1.0,
	    {.label = "Bounce threshold",
	     .category = "Solver",
	     .description = "Approach speed in m/s below which restitution is ignored",
	     .min = 0.0,
	     .max = 10.0,
	     .step = 0.1},
	    &Tunables::bounce_threshold);

	flt("physics.sleep.linear_threshold",
	    0.05,
	    {.label = "Linear threshold",
	     .category = "Sleep",
	     .description = "Speed in m/s a body must stay under to be eligible for sleep",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.005},
	    &Tunables::sleep_linear_threshold);
	flt("physics.sleep.angular_threshold",
	    0.05,
	    {.label = "Angular threshold",
	     .category = "Sleep",
	     .description = "Angular speed in rad/s a body must stay under to be eligible for sleep",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.005},
	    &Tunables::sleep_angular_threshold);
	flt("physics.sleep.delay",
	    0.5,
	    {.label = "Delay",
	     .category = "Sleep",
	     .description = "Seconds a body must stay under both thresholds before it sleeps",
	     .min = 0.0,
	     .max = 10.0,
	     .step = 0.1},
	    &Tunables::sleep_delay);
	flt("physics.sleep.drift",
	    0.05,
	    {.label = "Drift",
	     .category = "Sleep",
	     .description = "Meters the farthest point of a body may travel during the delay and still sleep",
	     .min = 0.005,
	     .max = 0.5,
	     .step = 0.005},
	    &Tunables::sleep_drift);

	toast::settings::declareFloat(
	    "physics.step.frequency",
	    60.0,
	    {.label = "Frequency", .category = "Step", .description = "Fixed steps per second", .min = 15.0, .max = 240.0, .step = 1.0}
	)
	    .onChange([](double v) { g_tunables.frequency = v > 0.0 ? v : 60.0; });
	uint_setting(
	    "physics.step.max_substeps",
	    8,
	    {.label = "Max substeps",
			 .category = "Step",
			 .description = "Ceiling on catch-up steps in a single frame",
			 .min = 1.0,
			 .max = 32.0,
			 .step = 1.0},
	    &Tunables::max_substeps
	);
	toast::settings::declareFloat(
	    "physics.step.max_burst_seconds",
	    0.1,
	    {.label = "Max burst seconds",
			 .category = "Step",
			 .description = "Real-time budget for a fixed-step catch-up burst after its first step",
			 .min = 0.0,
			 .max = 1.0,
			 .step = 0.01}
	)
	    .onChange([](double v) { g_tunables.max_burst_seconds = std::max(v, 0.0); });

	flt("physics.broadphase.fat_margin",
	    0.1,
	    {.label = "Fat margin",
	     .category = "Broadphase",
	     .description = "Slack in metres added to a shape's bounds in the tree",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.01},
	    &Tunables::broadphase_fat_margin);

	uint_setting(
	    "physics.fracture.max_connectivity_jobs_per_tick",
	    24,
	    {.label = "Max connectivity jobs per tick",
			 .category = "Fracture",
			 .description = "Concurrent connectivity analyses started in one physics tick",
			 .min = 1.0,
			 .max = 256.0,
			 .step = 1.0},
	    &Tunables::max_connectivity_jobs_per_tick
	);
	uint_setting(
	    "physics.fracture.max_spawns_per_step",
	    8,
	    {.label = "Max fragment spawns per step",
			 .category = "Fracture",
			 .description = "Fragments promoted to their own bodies in one step",
			 .min = 0.0,
			 .max = 64.0,
			 .step = 1.0},
	    &Tunables::max_fragment_spawns_per_step
	);
	uint_setting(
	    "physics.fracture.max_active_fragments",
	    96,
	    {.label = "Max active fragments",
			 .category = "Fracture",
			 .description = "Awake fragment budget before nearly settled debris is forced to sleep",
			 .min = 0.0,
			 .max = 16384.0,
			 .step = 64.0},
	    &Tunables::max_active_fragments
	);
	uint_setting(
	    "physics.fracture.pool_headroom",
	    4096,
	    {.label = "Fragment pool headroom",
			 .category = "Fracture",
			 .description = "Bricks kept spare for fragments",
			 .min = 0.0,
			 .max = 65536.0,
			 .step = 256.0},
	    &Tunables::fragment_pool_headroom
	);
	uint_setting(
	    "physics.fracture.min_fragment_voxels",
	    4,
	    {.label = "Min fragment voxels",
			 .category = "Fracture",
			 .description = "Disconnected pieces smaller than this are discarded instead of becoming debris",
			 .min = 1.0,
			 .max = 256.0,
			 .step = 1.0},
	    &Tunables::min_fragment_voxels
	);
	uint_setting(
	    "physics.fracture.despawn_max_voxels",
	    16,
	    {.label = "Despawn max voxels",
			 .category = "Fracture",
			 .description = "Largest settled fragment eligible for automatic despawning",
			 .min = 0.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::fragment_despawn_max_voxels
	);
	uint_setting(
	    "physics.fracture.large_debris_min_voxels",
	    16,
	    {.label = "Large debris min voxels",
			 .category = "Fracture",
			 .description = "Fragments with at least this many voxels go on the Large debris layer instead of Small debris",
			 .min = 1.0,
			 .max = 4096.0,
			 .step = 1.0},
	    &Tunables::large_debris_min_voxels
	);
	flt("physics.fracture.despawn_settle_seconds",
	    3.0,
	    {.label = "Despawn settle time",
	     .category = "Fracture",
	     .description = "Seconds a small fragment must remain asleep before despawning",
	     .min = 0.0,
	     .max = 60.0,
	     .step = 0.25},
	    &Tunables::fragment_despawn_settle_seconds);
	flt("physics.fracture.force_sleep_slack",
	    4.0,
	    {.label = "Force sleep slack",
	     .category = "Fracture",
	     .description = "Multiplier on sleep velocity thresholds when enforcing the active fragment budget",
	     .min = 1.0,
	     .max = 64.0,
	     .step = 0.5},
	    &Tunables::force_sleep_slack);
	flt("physics.fracture.sleep_scale",
	    3.0,
	    {.label = "Fragment sleep scale",
	     .category = "Fracture",
	     .description = "Multiplier on the sleep thresholds for fragments so debris settles sooner than other bodies",
	     .min = 1.0,
	     .max = 20.0,
	     .step = 0.5},
	    &Tunables::fragment_sleep_scale);
	toast::settings::declareInt(
	    "physics.fracture.max_fragment_extent_bricks",
	    1,
	    {.label = "Max fragment extent (bricks)",
			 .category = "Fracture",
			 .description = "Largest piece, in bricks along any axis, still treated as a fragment",
			 .min = 1.0,
			 .max = 16.0,
			 .step = 1.0}
	)
	    .onChange([](int64_t v) { g_tunables.max_fragment_extent_bricks = static_cast<int32_t>(std::max<int64_t>(v, 1)); });
	flt("physics.fracture.shell_voxels",
	    1.0,
	    {.label = "Fracture shell voxels",
	     .category = "Fracture",
	     .description = "Thickness in voxels of the shell a fracture carves around an impact",
	     .min = 0.0,
	     .max = 16.0,
	     .step = 0.5},
	    &Tunables::fracture_shell_voxels);
	flt("physics.fracture.depth_share",
	    0.5,
	    {.label = "Depth share",
	     .category = "Fracture",
	     .description = "Energy left after the fracture pokes through",
	     .min = 0.0,
	     .max = 1.0,
	     .step = 0.05},
	    &Tunables::fracture_depth_share);
	flt("physics.fracture.max_depth",
	    2.0,
	    {.label = "Max depth",
	     .category = "Fracture",
	     .description = "Meters a single hit can dig past its fracture radius",
	     .min = 0.0,
	     .max = 16.0,
	     .step = 0.1},
	    &Tunables::fracture_max_depth);
	flt("physics.fracture.max_overshoot",
	    2.0,
	    {.label = "Max overshoot",
	     .category = "Fracture",
	     .description = "Most a hit can widen past its fracture radius",
	     .min = 0.0,
	     .max = 16.0,
	     .step = 0.1},
	    &Tunables::fracture_max_overshoot);
	flt("physics.fracture.explosion_max_radius",
	    8.0,
	    {.label = "Explosion max radius",
	     .category = "Fracture",
	     .description = "Meters an explosion can break or push things at, unless it sets its own radius",
	     .min = 0.0,
	     .max = 64.0,
	     .step = 0.5},
	    &Tunables::explosion_max_radius);
	flt("physics.fracture.explosion_falloff",
	    2.0,
	    {.label = "Explosion falloff",
	     .category = "Fracture",
	     .description = "2 is the inverse square",
	     .min = 0.5,
	     .max = 8.0,
	     .step = 0.25},
	    &Tunables::explosion_falloff);
	flt("physics.fracture.explosion_chunk_size",
	    1.0,
	    {.label = "Explosion chunk size",
	     .category = "Fracture",
	     .description = "Meters across the chunks an explosion breaks material into",
	     .min = 0.2,
	     .max = 4.0,
	     .step = 0.1},
	    &Tunables::explosion_chunk_size);
	flt("physics.fracture.explosion_debris_ratio",
	    8.0,
	    {.label = "Explosion debris ratio",
	     .category = "Fracture",
	     .description = "Material hit by a blast this many times its toughness turns to debris instead of chunks",
	     .min = 1.0,
	     .max = 64.0,
	     .step = 0.5},
	    &Tunables::explosion_debris_ratio);
	flt("physics.fracture.explosion_shatter_width",
	    0.6,
	    {.label = "Explosion shatter width",
	     .category = "Fracture",
	     .description = "Meters of cracked material around the hole an explosion leaves, 0 for none",
	     .min = 0.0,
	     .max = 4.0,
	     .step = 0.1},
	    &Tunables::explosion_shatter_width);
	flt("physics.fracture.explosion_max_speed",
	    20.0,
	    {.label = "Explosion max speed",
	     .category = "Fracture",
	     .description = "Fastest an explosion throws a chunk, debris or body, in m/s. Faster tunnels through thin colliders",
	     .min = 1.0,
	     .max = 100.0,
	     .step = 1.0},
	    &Tunables::explosion_max_speed);
	uint_setting(
	    "physics.fracture.explosion_max_fragments",
	    48,
	    {.label = "Explosion max fragments",
			 .category = "Fracture",
			 .description = "Chunks one explosion can promote to their own bodies per shape; the rest become debris",
			 .min = 0.0,
			 .max = 256.0,
			 .step = 1.0},
	    &Tunables::explosion_max_fragments
	);
	uint_setting(
	    "physics.fracture.impact_max_hardness",
	    1,
	    {.label = "Impact max hardness",
			 .category = "Fracture",
			 .description = "Hardest material collisions between bodies can break",
			 .min = 0.0,
			 .max = 2.0,
			 .step = 1.0},
	    &Tunables::impact_max_hardness
	);
	flt("physics.fracture.bond_cost",
	    1.0,
	    {.label = "Bond cost", .category = "Fracture", .min = 0.0, .max = 16.0, .step = 0.1},
	    &Tunables::fracture_bond_cost);
	uint_setting(
	    "physics.fracture.max_unglue_voxels",
	    4096,
	    {.label = "Max unglue voxels", .category = "Fracture", .min = 0.0, .max = 1000000.0, .step = 64.0},
	    &Tunables::fracture_max_unglue_voxels
	);
	uint_setting(
	    "physics.fracture.max_shatter_cells",
	    128,
	    {.label = "Max shatter cells", .category = "Shatter", .min = 2.0, .max = 1024.0, .step = 8.0},
	    &Tunables::fracture_max_shatter_cells
	);
	flt("physics.fracture.max_shatter_distance",
	    8.0,
	    {.label = "Max shatter distance", .category = "Shatter", .min = 0.1f, .max = 64.0f, .step = 0.5f},
	    &Tunables::fracture_max_shatter_distance);
	uint_setting(
	    "physics.debris.max_spawns_per_step",
	    512,
	    {.label = "Max spawns per step", .category = "Debris", .min = 0.0, .max = 16384.0, .step = 16.0},
	    &Tunables::debris_max_spawns_per_step
	);
	flt("physics.debris.speed_scale",
	    1.0,
	    {.label = "Speed scale", .category = "Debris", .min = 0.0f, .max = 8.0f, .step = 0.1f},
	    &Tunables::debris_speed_scale);
	flt("physics.debris.lifetime",
	    12.0,
	    {.label = "Lifetime", .category = "Debris", .min = 0.5, .max = 600.0, .step = 0.5},
	    &Tunables::debris_lifetime);
	flt("physics.debris.lifetime_jitter",
	    0.3,
	    {.label = "Lifetime jitter",
	     .category = "Debris",
	     .description = "Each piece lives lifetime times one plus or minus this so a burst does not vanish at once",
	     .min = 0.0,
	     .max = 0.9,
	     .step = 0.05},
	    &Tunables::debris_lifetime_jitter);
	uint_setting(
	    "physics.debris.max_particles",
	    16384,
	    {.label = "Max particles",
			 .category = "Debris",
			 .description = "Past this the oldest pieces shrink away to make room for new ones",
			 .min = 256.0,
			 .max = 262144.0,
			 .step = 1024.0,
			 .requires_restart = true},
	    &Tunables::debris_max_particles
	);
	flt("physics.debris.restitution",
	    0.3,
	    {.label = "Restitution", .category = "Debris", .min = 0.0, .max = 1.0, .step = 0.05},
	    &Tunables::debris_restitution);
	flt("physics.debris.friction",
	    0.6,
	    {.label = "Friction", .category = "Debris", .min = 0.0, .max = 1.0, .step = 0.05},
	    &Tunables::debris_friction);

	uint_setting(
	    "physics.jobs.min_bounds_per_job",
	    32,
	    {.label = "Min bounds per job",
			 .category = "Jobs",
			 .description = "Broadphase bounds updates below this run inline - a job costs more than the work",
			 .min = 1.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::min_bounds_per_job
	);
	uint_setting(
	    "physics.jobs.min_candidates_per_job",
	    2,
	    {.label = "Min candidates per job",
			 .category = "Jobs",
			 .description = "Narrow phase candidate pairs below this run inline",
			 .min = 1.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::min_candidates_per_job
	);
	uint_setting(
	    "physics.jobs.min_manifolds_per_job",
	    4,
	    {.label = "Min manifolds per job",
			 .category = "Jobs",
			 .description = "Active manifolds below this run constraint preparation inline",
			 .min = 1.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::min_manifolds_per_job
	);
	uint_setting(
	    "physics.jobs.min_shapes_per_query_job",
	    16,
	    {.label = "Min shapes per query job",
			 .category = "Jobs",
			 .description = "Leaf shapes below this run broadphase tree queries inline",
			 .min = 1.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::min_shapes_per_query_job
	);
	uint_setting(
	    "physics.jobs.min_bodies_per_job",
	    64,
	    {.label = "Min bodies per job",
			 .category = "Jobs",
			 .description = "Bodies below this run integration inline instead of across the thread pool",
			 .min = 1.0,
			 .max = 4096.0,
			 .step = 1.0},
	    &Tunables::min_bodies_per_job
	);
	uint_setting(
	    "physics.jobs.min_wave_constraints_for_dispatch",
	    512,
	    {.label = "Min wave constraints per dispatch",
			 .category = "Jobs",
			 .description = "Constraint waves below this size solve inline",
			 .min = 1.0,
			 .max = 65536.0,
			 .step = 1.0},
	    &Tunables::min_wave_constraints_for_dispatch
	);
	uint_setting(
	    "physics.jobs.box_voxel_split_min_voxels",
	    500000,
	    {.label = "Box-voxel split threshold",
			 .category = "Jobs",
			 .description = "Box-voxel pairs above this surface area split across jobs, kept high until measured",
			 .min = 1.0,
			 .max = 1000000.0,
			 .step = 1.0},
	    &Tunables::box_voxel_split_min_voxels
	);
	uint_setting(
	    "physics.jobs.voxel_voxel_split_min_voxels",
	    250000,
	    {.label = "Voxel-voxel split threshold",
			 .category = "Jobs",
			 .description = "Same reasoning and same deliberately-off-for-now state as box_voxel_split_min_voxels",
			 .min = 1.0,
			 .max = 1000000.0,
			 .step = 1.0},
	    &Tunables::voxel_voxel_split_min_voxels
	);
	uint_setting(
	    "physics.jobs.voxel_pair_split_regions",
	    8,
	    {.label = "Voxel pair split regions",
			 .category = "Jobs",
			 .description = "How many jobs a split box-voxel or voxel-voxel pair divides into",
			 .min = 1.0,
			 .max = 64.0,
			 .step = 1.0},
	    &Tunables::voxel_pair_split_regions
	);
	uint_setting(
	    "physics.character.sweep_refine_iterations",
	    6,
	    {.label = "Sweep refine iterations",
			 .category = "Character",
			 .description = "Bisection steps a sweep refines by once it hits something, a clear sweep never pays it",
			 .min = 1.0,
			 .max = 32.0,
			 .step = 1.0},
	    &Tunables::sweep_refine_iterations
	);
}

}
