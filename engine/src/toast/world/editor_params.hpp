/**
 * @file editor_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief How someone likes the editor to behave
 */

#pragma once
#include <algorithm>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace toast {

struct EditorParams : params::Group<"editor"> {
	using Kind = params::Kind;

	float camera_default_speed = param(
	    &camera_default_speed, "camera.default_speed", 5.0F,
	    {.name = "Start speed",
			 .min = 0.1F,
			 .max = 500.0F,
			 .unit = "m/s",
			 .kind = Kind::user,
			 .description = "Fly speed a new viewport starts at"}
	);
	float camera_min_speed = param(
	    &camera_min_speed, "camera.min_speed", 0.5F,
	    {.name = "Slowest speed",
			 .min = 0.01F,
			 .max = 100.0F,
			 .unit = "m/s",
			 .kind = Kind::user,
			 .description = "The slowest the scroll wheel goes"}
	);
	float camera_max_speed = param(
	    &camera_max_speed, "camera.max_speed", 50.0F,
	    {.name = "Fastest speed",
			 .min = 1.0F,
			 .max = 5000.0F,
			 .unit = "m/s",
			 .kind = Kind::user,
			 .description = "The fastest the scroll wheel goes"}
	);
	float camera_boost = param(
	    &camera_boost, "camera.boost", 3.0F,
	    {.name = "Boost",
			 .min = 1.0F,
			 .max = 50.0F,
			 .kind = Kind::user,
			 .description = "Speed multiplier while the boost key is held"}
	);
	float camera_look_sensitivity = param(
	    &camera_look_sensitivity, "camera.look_sensitivity", 0.0025F,
	    {.name = "Look sensitivity",
			 .min = 0.0002F,
			 .max = 0.05F,
			 .kind = Kind::user,
			 .description = "Radians turned per pixel of mouse"}
	);
	float camera_zoom_sensitivity = param(
	    &camera_zoom_sensitivity, "camera.zoom_sensitivity", 0.01F,
	    {.name = "Zoom sensitivity", .min = 0.001F, .max = 0.5F, .kind = Kind::user}
	);
	float camera_pan_sensitivity = param(
	    &camera_pan_sensitivity, "camera.pan_sensitivity", 0.01F,
	    {.name = "Pan sensitivity", .min = 0.001F, .max = 0.5F, .kind = Kind::user}
	);
	float camera_orbit_min_radius = param(
	    &camera_orbit_min_radius, "camera.orbit_min_radius", 0.1F,
	    {.name = "Closest orbit",
			 .min = 0.01F,
			 .max = 10.0F,
			 .unit = "m",
			 .kind = Kind::user,
			 .description = "Closest an orbiting camera gets to its target"}
	);
	bool autosave_enabled = param(
	    &autosave_enabled, "autosave.enabled", true,
	    {.name = "Autosave", .kind = Kind::user, .description = "Keep a copy of unsaved work to recover from a crash"}
	);
	int autosave_interval = param(
	    &autosave_interval, "autosave.interval_seconds", 60,
	    {.name = "Autosave every", .min = 10, .max = 3600, .unit = "s", .kind = Kind::user, .description = "Between autosaves"}
	);
	int git_fetch_interval = param(
	    &git_fetch_interval, "git.fetch_seconds", 300,
	    {.name = "Fetch every",
			 .min = 10,
			 .max = 7200,
			 .unit = "s",
			 .kind = Kind::user,
			 .description = "Between checks for new commits"}
	);
	int git_locks_interval = param(
	    &git_locks_interval, "git.locks_seconds", 60,
	    {.name = "Refresh locks every",
			 .min = 10,
			 .max = 600,
			 .unit = "s",
			 .kind = Kind::user,
			 .description = "Between lock refreshes"}
	);
	bool snap_translate_enabled = param(
	    &snap_translate_enabled, "snapping.translate_enabled", true,
	    {.name = "Move snapping", .kind = Kind::user, .description = "On for a new viewport"}
	);
	float snap_translate_step = param(
	    &snap_translate_step, "snapping.translate_step", 0.1F,
	    {.name = "Move step",
			 .min = 0.001F,
			 .max = 100.0F,
			 .unit = "m",
			 .kind = Kind::user,
			 .description = "Where a new viewport starts"}
	);
	bool snap_rotate_enabled = param(
	    &snap_rotate_enabled, "snapping.rotate_enabled", true,
	    {.name = "Rotate snapping", .kind = Kind::user, .description = "On for a new viewport"}
	);
	float snap_rotate_step = param(
	    &snap_rotate_step, "snapping.rotate_step", 30.0F,
	    {.name = "Rotate step",
			 .min = 0.1F,
			 .max = 180.0F,
			 .unit = "°",
			 .kind = Kind::user,
			 .description = "Where a new viewport starts"}
	);
	bool snap_scale_enabled = param(
	    &snap_scale_enabled, "snapping.scale_enabled", true,
	    {.name = "Scale snapping", .kind = Kind::user, .description = "On for a new viewport"}
	);
	float snap_scale_step = param(
	    &snap_scale_step, "snapping.scale_step", 0.1F,
	    {.name = "Scale step", .min = 0.001F, .max = 10.0F, .kind = Kind::user, .description = "Where a new viewport starts"}
	);
	int logs_message_length = param(
	    &logs_message_length, "logs.max_message_length", 500,
	    {.name = "Longest log line",
			 .min = 80,
			 .max = 5000,
			 .unit = "chars",
			 .kind = Kind::user,
			 .description = "Longer log lines are cut in the list"}
	);
	float camera_orbit_max_radius = param(
	    &camera_orbit_max_radius, "camera.orbit_max_radius", 5000.0F,
	    {.name = "Farthest orbit", .min = 10.0F, .max = 100000.0F, .unit = "m", .kind = Kind::user}
	);
};

[[nodiscard]]
inline auto clampedBetween(float value, float low, float high) -> float {
	return std::clamp(value, low, std::max(low, high));
}

[[nodiscard]]
TOAST_API auto editorParams() -> EditorParams&;

}
