#include "physics_settings.hpp"

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
	    2.0,
	    {.label = "Fracture shell voxels",
	     .category = "Fracture",
	     .description = "Thickness in voxels of the shell a fracture carves around an impact",
	     .min = 0.0,
	     .max = 16.0,
	     .step = 0.5},
	    &Tunables::fracture_shell_voxels);

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
	    "physics.jobs.min_islands_per_job",
	    1,
	    {.label = "Min islands per job",
			 .category = "Jobs",
			 .description = "Solver islands below this run inline",
			 .min = 1.0,
			 .max = 1024.0,
			 .step = 1.0},
	    &Tunables::min_islands_per_job
	);
}

}
