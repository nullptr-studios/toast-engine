/**
 * @file renderer_params.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Every renderer setting
 */

#pragma once
#include "toast/renderer/lighting/shadow_constants.hpp"
#include "toast/renderer/lighting/shadow_slots.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/group.hpp>

namespace renderer {

namespace options {
inline constexpr std::array<std::string_view, 27> view_mode {
  "Lit",
  "ClusterHeatmap",
  "Albedo",
  "Normal",
  "MetallicRoughness",
  "Ambient",
  "SpecularIbl",
  "SpecularIblMip0",
  "Reflection",
  "ReflectionProbes",
  "ProbeCapture",
  "ProbeCubemap",
  "NormalBuffer",
  "RoughnessBuffer",
  "SsrOnly",
  "AmbientOcclusion",
  "IrradianceVolumes",
  "TracedShadowsOnly",
  "TracedShadows",
  "ShadowTerm",
  "VoxelSteps",
  "VoxelTraversal",
  "VoxelBricks",
  "VoxelVolumes",
  "VoxelMaterials",
  "VoxelPaletteId",
  "MotionVectors"
};
inline constexpr std::array<std::string_view, 9> quality_preset {
  "Low", "Medium", "High", "Epic", "Low RT", "Medium RT", "High RT", "Epic RT", "Custom"
};
inline constexpr std::array<std::string_view, 5> shadows_cascade_resolution {"256", "512", "1024", "2048", "4096"};
inline constexpr std::array<std::string_view, 5> shadows_punctual_resolution {"256", "512", "1024", "2048", "4096"};
inline constexpr std::array<std::string_view, 5> volumetrics_quality {"Low", "Medium", "High", "Epic", "Custom"};
inline constexpr std::array<std::string_view, 2> volumetrics_resolution {"Half", "Quarter"};
inline constexpr std::array<std::string_view, 3> quality_ray_tracing {"Auto", "On", "Off"};
inline constexpr std::array<std::string_view, 5> gi_quality {"Low", "Medium", "High", "Epic", "Custom"};
inline constexpr std::array<std::string_view, 3> gi_ray_resolution {"Full", "Half", "Quarter"};
inline constexpr std::array<std::string_view, 7> gi_debug_view {
  "Off", "Cascade 0", "Cascade 1", "Cascade 2", "Cascade 3", "Cascade 4", "Cascade 5"
};
inline constexpr std::array<std::string_view, 6> gi_debug_mode {
  "Probe cells", "Interval radiance", "Coverage", "Irradiance", "History depth", "Irradiance change"
};
inline constexpr std::array<std::string_view, 2> gi_backend {"Radiance cascades", "Radiance clipmap"};
inline constexpr std::array<std::string_view, 3> gi_clip_resolution {"64", "128", "192"};
inline constexpr std::array<std::string_view, 7> gi_clip_debug_view {
  "Off", "Level 0", "Level 1", "Level 2", "Level 3", "Level 4", "Level 5"
};
inline constexpr std::array<std::string_view, 4> gi_clip_debug_mode {"Albedo", "Emission", "Opacity", "Radiance"};
}

#if defined(TOAST_DEV_TOOLS)
inline constexpr int64_t k_default_preset = 8;
#else
inline constexpr int64_t k_default_preset = 1;
#endif

struct RendererParams : toast::params::Group<"renderer"> {
	template<typename T>
	using Param = toast::params::Param<T>;
	using Kind = toast::params::Kind;
	using AssetRef = toast::params::AssetRef;

	Param<bool> display_clamp_to_simulation {
	  "display.clamp_to_simulation",
	  true,
	  {.name = "Clamp to game thread",
		  .description = "Wait for a new game frame instead of drawing the same one again. Off runs the GPU flat out"}
	};
	Param<bool> display_low_latency {
	  "display.low_latency",
	  true,
	  {.name = "Low latency",
		  .description = "Wait for the GPU to finish a frame before starting the next, so input reaches the screen sooner"}
	};
	Param<double> display_frame_rate_limit {
	  "display.frame_rate_limit",
	  0.0,
	  {.name = "Frame rate limit", .min = 0.0, .max = 360.0, .unit = "fps", .description = "Frames per second. 0 is uncapped"}
	};
	Param<double> display_hdr_paper_white {
	  "display.hdr_paper_white",
	  0.0,
	  {.name = "HDR paper white",
		  .min = 0.0,
		  .max = 500.0,
		  .unit = "nits",
		  .description = "Nits of SDR white and the UI in HDR. 0 follows the Windows slider"}
	};
	Param<double> display_hdr_peak {
	  "display.hdr_peak",
	  0.0,
	  {.name = "HDR peak brightness",
		  .min = 0.0,
		  .max = 4000.0,
		  .unit = "nits",
		  .description = "Nits highlights roll off toward in HDR. 0 asks the display"}
	};
	Param<int64_t> quality_preset {
	  "quality.preset",
	  k_default_preset,
	  {.name = "Preset",
		  .description = "Sets the quality settings together. Custom uses each one",
		  .options = options::quality_preset}
	};
	Param<double> quality_render_scale {
	  "quality.render_scale",
	  1.0,
	  {.name = "Render scale",
		  .min = 0.5,
		  .max = 1.0,
		  .description = "Share of the screen resolution the world renders at. Custom preset only"}
	};
	Param<bool> quality_bloom {
	  "quality.bloom", true, {.name = "Bloom", .description = "Glow around bright highlights"}
	};
	Param<bool> quality_lens_flare {
	  "quality.lens_flare", true, {.name = "Lens flare", .description = "Ghosts and streaks from the brightest pixels"}
	};
	Param<double> quality_glass_glow_scale {
	  "quality.glass_glow_scale",
	  1.0,
	  {.name = "Emissive behind glass",
		  .min = 0.0,
		  .max = 16.0,
		  .description = "How much of the light behind glass bloom and flares still see. 1 is as if the glass was not there"}
	};
	Param<bool> quality_ssr {
	  "quality.ssr", true, {.name = "SSR", .description = "Screen space reflections. Custom preset only"}
	};
	Param<bool> quality_ssao {
	  "quality.ssao", true, {.name = "SSAO", .description = "Contact shadows in creases. Custom preset only"}
	};
	Param<bool> quality_motion_blur {
	  "quality.motion_blur", false, {.name = "Motion blur", .description = "Blur along motion. Custom preset only"}
	};
	Param<double> quality_motion_blur_strength {
	  "quality.motion_blur_strength",
	  0.5,
	  {.name = "Motion blur shutter",
		  .min = 0.0,
		  .max = 1.0,
		  .description = "How much of the frame the shutter is open. 0.5 is a 180 degree shutter"}
	};
	Param<double> quality_motion_blur_max_length {
	  "quality.motion_blur_max_length",
	  0.04,
	  {.name = "Motion blur max length", .min = 0.005, .max = 0.2, .description = "Longest blur, as a share of the screen height"}
	};
	Param<int64_t> quality_motion_blur_samples {
	  "quality.motion_blur_samples",
	  12,
	  {.name = "Motion blur samples", .min = 4, .max = 32, .description = "Taps per blur. More is smoother and slower"}
	};
	Param<double> quality_ssr_reach {
	  "quality.ssr_reach",
	  0.0,
	  {.name = "SSR reach", .min = 0.0, .max = 500.0, .unit = "m", .description = "Farthest an SSR ray travels. Custom preset only"}
	};
	Param<int64_t> quality_ssr_steps {
	  "quality.ssr_steps",
	  0,
	  {.name = "SSR steps",
		  .min = 0,
		  .max = 256,
		  .description = "Steps per SSR ray. 0 uses the post process volume. Custom preset only"}
	};
	Param<bool> quality_fxaa {
	  "quality.fxaa", true, {.name = "FXAA", .description = "Cheap edge smoothing. Off while TAA is on"}
	};
	Param<bool> quality_taa {
	  "quality.taa", true, {.name = "TAA", .description = "Smooths edges over several frames"}
	};
	Param<double> quality_taa_history {
	  "quality.taa_history",
	  0.9,
	  {.name = "TAA history",
		  .min = 0.5,
		  .max = 0.98,
		  .description = "How much of the last frame each pixel keeps. More is smoother and ghosts more"}
	};
	Param<bool> quality_traced_shadows {
	  "quality.traced_shadows",
	  false,
	  {.name = "Traced shadows", .description = "Trace shadow rays instead of reading shadow maps. Needs ray queries"}
	};
	Param<int64_t> shadows_cascade_resolution {
	  "shadows.cascade_resolution",
	  2,
	  {.name = "Cascade resolution",
		  .unit = "px",
		  .description = "Size of each cascade's shadow map",
		  .options = options::shadows_cascade_resolution,
		  .requires_restart = true}
	};
	Param<int64_t> shadows_punctual_resolution {
	  "shadows.punctual_resolution",
	  1,
	  {.name = "Punctual resolution",
		  .unit = "px",
		  .description = "Size of spot and point shadow maps",
		  .options = options::shadows_punctual_resolution,
		  .requires_restart = true}
	};
	Param<double> shadows_distance {
	  "shadows.distance",
	  shadows::k_shadow_distance,
	  {.name = "Shadow distance",
		                     .min = 10.0,
		                     .max = 1000.0,
		                     .unit = "m",
		                     .description = "How far from the camera directional shadows reach"}
	};
	Param<double> shadows_slot_retention {
	  "shadows.slot_retention",
	  shadow_slots::k_default_retention,
	  {.name = "Slot retention",
		                       .min = 1.0,
		                       .max = 4.0,
		                       .kind = Kind::advanced,
		                       .description = "How much more a light must matter to take another's shadow slot"}
	};
	Param<int64_t> shadows_slot_grace {
	  "shadows.slot_grace",
	  shadow_slots::k_default_grace_frames,
	  {.name = "Slot grace",
		                          .min = 0,
		                          .max = 300,
		                          .unit = "frames",
		                          .kind = Kind::advanced,
		                          .description = "Frames a light keeps its slot after leaving view"}
	};
	Param<int64_t> shadows_spot_slots {
	  "shadows.spot_slots",
	  shadows::k_default_spot_shadows,
	  {.name = "Spot shadow slots",
		                          .min = 0,
		                          .max = static_cast<double>(shadows::k_max_spot_shadows),
		                          .description = "Spot lights that can hold a shadow, about 3 MB each at 512",
		                          .requires_restart = true}
	};
	Param<int64_t> shadows_point_slots {
	  "shadows.point_slots",
	  shadows::k_default_point_shadows,
	  {.name = "Point shadow slots",
		                           .min = 0,
		                           .max = static_cast<double>(shadows::k_max_point_shadows),
		                           .description = "Point lights that can hold a shadow, about 18 MB each at 512",
		                           .requires_restart = true}
	};
	Param<int64_t> shadows_spot_budget {
	  "shadows.spot_budget",
	  shadows::k_max_spot_shadows,
	  {.name = "Spot lights casting",
		                      .min = 0,
		                      .max = static_cast<double>(shadows::k_max_spot_shadows),
		                      .description = "Spot shadows used at once, nearest first"}
	};
	Param<int64_t> shadows_point_budget {
	  "shadows.point_budget",
	  shadows::k_max_point_shadows,
	  {.name = "Point lights casting",
		                       .min = 0,
		                       .max = static_cast<double>(shadows::k_max_point_shadows),
		                       .description = "Point shadows used at once, nearest first"}
	};
	Param<double> shadows_full_resolution_ranges {
	  "shadows.full_resolution_ranges",
	  shadows::k_punctual_full_resolution_ranges,
	  {.name = "Full resolution reach",
		                                     .min = 0.5,
		                                     .max = 16.0,
		                                     .kind = Kind::advanced,
		                                     .description = "Distance, in light ranges, at which a shadow map starts to shrink"}
	};
	Param<double> shadows_cascade_split_lambda {
	  "shadows.cascade_split_lambda",
	  shadows::k_cascade_split_lambda,
	  {.name = "Cascade split", .min = 0.0, .max = 1.0, .description = "0 splits the cascades evenly, 1 logarithmically"}
	};
	Param<double> shadows_cascade_padding {
	  "shadows.cascade_padding",
	  shadows::k_cascade_padding,
	  {.name = "Cascade padding",
		                     .min = 0.0,
		                     .max = 0.5,
		                     .description = "Extra margin per cascade, so it re-renders less while the camera moves"}
	};
	Param<double> shadows_cascade_receiver_depth {
	  "shadows.cascade_receiver_depth",
	  shadows::k_cascade_receiver_depth_radii,
	  {.name = "Cascade receiver depth",
		                                  .min = 1.0,
		                                  .max = 16.0,
		                                  .kind = Kind::advanced,
		                                  .description = "How far along the light each cascade reaches, in cascade radii"}
	};
	Param<bool> shadows_static_cache {
	  "shadows.static_cache", true, {.name = "Static caster cache", .description = "Cache resting voxel volumes in the shadow map"}
	};
	Param<double> shadows_caster_tolerance {
	  "shadows.caster_tolerance",
	  shadows::k_caster_tolerance,
	  {.name = "Caster tolerance",
		                      .min = 0.0,
		                      .max = 0.1,
		                      .unit = "m",
		                      .description = "How far a voxel volume drifts before its shadow follows"}
	};
	Param<bool> shadows_voxel_mesh_shaders {
	  "shadows.voxel_mesh_shaders",
	  true,
	  {.name = "Mesh shader voxel casters", .description = "Draw voxel shadow casters with mesh shaders when the GPU has them"}
	};
	Param<int64_t> voxels_light_candidates {
	  "voxels.light_candidates",
	  8,
	  {.name = "Light candidates",
		  .min = 1,
		  .max = 32,
		  .kind = Kind::advanced,
		  .description = "Lights sampled per pixel for marched voxel shadows"}
	};
	Param<bool> voxels_shadow_maps_only {
	  "voxels.shadow_maps_only",
	  false,
	  {.name = "Shadow maps only", .description = "Voxels use shadow maps for every light. Custom preset only"}
	};
	Param<bool> voxels_ambient_rays {
	  "voxels.ambient_rays", true, {.name = "Ambient rays", .description = "Ambient occlusion from rays instead of SSAO on voxels"}
	};
	Param<bool> voxels_ambient_ray_query {
	  "voxels.ambient_ray_query",
	  true,
	  {.name = "Ray query ambient rays",
		  .kind = Kind::advanced,
		  .description = "Use ray queries for ambient rays when the GPU has them"}
	};
	Param<double> voxels_ambient_reach {
	  "voxels.ambient_reach",
	  2.4,
	  {.name = "Ambient reach",
		  .min = 0.2,
		  .max = 20.0,
		  .unit = "m",
		  .description = "How far an ambient ray looks before it counts as open sky"}
	};
	Param<bool> voxels_traced_specular {
	  "voxels.traced_specular",
	  true,
	  {.name = "Traced specular", .description = "Trace reflections on shiny voxels instead of the environment and SSR"}
	};
	Param<double> voxels_specular_roughness {
	  "voxels.specular_roughness",
	  0.6,
	  {.name = "Traced specular roughness", .min = 0.0, .max = 1.0, .description = "Rougher voxels keep the environment reflection"}
	};
	Param<double> voxels_specular_reach {
	  "voxels.specular_reach",
	  32.0,
	  {.name = "Traced specular reach", .min = 1.0, .max = 200.0, .unit = "m", .description = "How far a reflection ray travels"}
	};
	Param<bool> voxels_specular_hit_lights {
	  "voxels.specular_hit_lights",
	  true,
	  {.name = "Traced specular hit lights",
		  .description = "Reflection hits off screen are lit by local lights. Custom preset only"}
	};
	Param<int64_t> voxels_glass_lit_surfaces {
	  "voxels.glass_lit_surfaces",
	  4,
	  {.name = "Glass lit surfaces",
		  .min = 1,
		  .max = 4,
		  .description = "Glass layers per pixel that get full lighting and reflections"}
	};
	Param<double> voxels_glass_reflection_reach {
	  "voxels.glass_reflection_reach",
	  32.0,
	  {.name = "Glass reflection reach", .min = 0.0, .max = 128.0, .unit = "m", .description = "How far glass reflections reach"}
	};
	Param<double> voxels_glass_coarse_distance {
	  "voxels.glass_coarse_distance",
	  30.0,
	  {.name = "Glass coarse shading distance",
		  .min = 0.0,
		  .max = 500.0,
		  .unit = "m",
		  .description = "Past this distance glass shades one sample per 2x2 pixels. 0 shades every pixel"}
	};
	Param<bool> voxels_glass_shadows {
	  "voxels.glass_shadows", true, {.name = "Glass shadows", .description = "Glass tints the light that reaches voxels behind it"}
	};
	Param<int64_t> voxels_glass_shadow_reach {
	  "voxels.glass_shadow_reach",
	  50,
	  {.name = "Glass shadow reach",
		  .min = 1,
		  .max = 2000,
		  .unit = "m",
		  .description = "How far a sun looks for glass above a voxel"}
	};
	Param<int64_t> voxels_sun_reach {
	  "voxels.sun_reach",
	  200,
	  {.name = "Sun ray reach", .min = 10, .max = 2000, .unit = "m", .description = "How far a marched sun shadow ray travels"}
	};
	Param<AssetRef> voxels_detail_texture {
	  "voxels.detail_texture",
	  AssetRef {},
	  {.name = "Voxel detail texture",
		          .description = "Detail texture the voxel palette reads. None keeps the neutral one",
		          .asset_type = "detail_texture"}
	};
	Param<bool> voxels_trace_bricks {
	  "voxels.trace_bricks",
	  true,
	  {.name = "Brick BLAS", .kind = Kind::advanced, .description = "Rays find occupied bricks instead of whole volumes"}
	};
	Param<int64_t> voxels_blas_budget {
	  "voxels.blas_budget",
	  262144,
	  {.name = "BLAS bricks per frame",
		  .min = 4096,
		  .max = 4194304,
		  .unit = "bricks",
		  .kind = Kind::advanced,
		  .description = "Bricks of acceleration structure built per frame"}
	};
	Param<double> voxels_trace_cell_size {
	  "voxels.trace_cell_size",
	  4.0,
	  {.name = "Trace grid cell",
		  .min = 0.5,
		  .max = 32.0,
		  .unit = "m",
		  .kind = Kind::advanced,
		  .description = "Cell size of the grid marched rays walk"}
	};
	Param<int64_t> voxels_shadow_instances {
	  "voxels.shadow_instances",
	  256,
	  {.name = "Shadow ray instances",
		  .min = 1,
		  .max = 4096,
		  .kind = Kind::advanced,
		  .description = "Nearest volumes a marched shadow ray tests"}
	};
	Param<bool> voxels_cell_boxes {
	  "voxels.cell_boxes",
	  true,
	  {.name = "Cell boxes", .kind = Kind::advanced, .description = "Draw a box per occupied cell instead of one per volume"}
	};
	Param<bool> voxels_occlusion {
	  "voxels.occlusion",
	  true,
	  {.name = "Occlusion culling",
		  .kind = Kind::advanced,
		  .description = "Skip cells hidden behind closer geometry. Needs cell boxes"}
	};
	Param<bool> voxels_mesh_shaders {
	  "voxels.mesh_shaders",
	  true,
	  {.name = "Mesh shaders",
		  .kind = Kind::advanced,
		  .description = "Mesh voxels near the camera with mesh shaders when the GPU has them"}
	};
	Param<double> voxels_raster_pixels {
	  "voxels.raster_pixels",
	  4.0,
	  {.name = "Raster voxel size",
		  .min = 0.0,
		  .max = 32.0,
		  .unit = "px",
		  .kind = Kind::advanced,
		  .description = "Cells whose voxels cover this many pixels are meshed instead of marched"}
	};
	Param<double> voxels_occlusion_min_hidden {
	  "voxels.occlusion_min_hidden",
	  0.05,
	  {.name = "Occlusion idle below",
		  .min = 0.0,
		  .max = 0.5,
		  .kind = Kind::advanced,
		  .description = "Share of cells occlusion must hide to stay on. 0 never pauses it"}
	};
	Param<int64_t> voxels_occlusion_idle_frames {
	  "voxels.occlusion_idle_frames",
	  120,
	  {.name = "Occlusion idle frames",
		  .min = 0,
		  .max = 600,
		  .unit = "frames",
		  .kind = Kind::advanced,
		  .description = "Frames occlusion stays paused before it measures again"}
	};
	Param<AssetRef> environment_map {
	  "environment.map",
	  AssetRef {},
	  {.name = "Environment map",
		          .description = "Equirectangular .hdr used for the sky and the lighting. None keeps the generated sky",
		          .asset_type = "environment_map"}
	};
	Param<double> environment_sky_intensity {
	  "environment.sky_intensity",
	  1.0,
	  {.name = "Sky intensity", .min = 0.0, .max = 20.0, .description = "Brightness of the generated sky"}
	};
	Param<bool> volumetrics_enabled {
	  "volumetrics.enabled", true, {.name = "Volumetrics", .description = "Fog where a post process volume sets a density"}
	};
	Param<int64_t> volumetrics_quality {
	  "volumetrics.quality",
	  2,
	  {.name = "Volumetric quality",
		  .description = "Steps, lights, shadows and resolution together. Custom uses the ones below",
		  .options = options::volumetrics_quality}
	};
	Param<int64_t> volumetrics_steps {
	  "volumetrics.steps",
	  24,
	  {.name = "Volumetric steps", .min = 4, .max = 96, .description = "Samples along each view ray. Custom quality only"}
	};
	Param<int64_t> volumetrics_lights {
	  "volumetrics.lights",
	  8,
	  {.name = "Volumetric lights", .min = 0, .max = 32, .description = "Local lights each step scatters. Custom quality only"}
	};
	Param<bool> volumetrics_light_shadows {
	  "volumetrics.light_shadows",
	  true,
	  {.name = "Volumetric light shadows", .description = "Local lights cast shadows into the fog. Custom quality only"}
	};
	Param<int64_t> volumetrics_resolution {
	  "volumetrics.resolution",
	  0,
	  {.name = "Volumetric resolution",
		  .description = "Fog resolution. Custom quality only",
		  .options = options::volumetrics_resolution}
	};
	Param<double> volumetrics_distance {
	  "volumetrics.distance",
	  100.0,
	  {.name = "Volumetric distance", .min = 10.0, .max = 1000.0, .unit = "m", .description = "How far fog volumes are visible"}
	};
	Param<bool> display_debug_overlay {
	  "display.debug_overlay",
	  false,
	  {.name = "Debug overlay in game",
		  .kind = Kind::debug,
		  .description = "Show the debug windows in the game and free the cursor. F3 toggles it"}
	};
	Param<int64_t> view_mode {
	  "view_mode",
	  0,
	  {.name = "View mode",
		  .kind = Kind::debug,
		  .description = "What the viewport shows instead of the lit scene, in the order of its Mode menu",
		  .options = options::view_mode}
	};
	Param<bool> editor_camera {
	  "editor_camera",
	  false,
	  {.name = "Editor camera",
		  .kind = Kind::debug,
		  .description =
		     "Look through a fly camera instead of the scene's own, in the editor or the game. WASD moves, Q and E go down and "
		     "up, Shift is faster, hold the right mouse button to look"}
	};
	Param<bool> voxels_glass_cost_view {
	  "voxels.glass_cost_view",
	  false,
	  {.name = "Glass cost view",
		  .kind = Kind::debug,
		  .description = "Colors each pixel by what its glass cost: green bricks, red lit surfaces, blue reflections"}
	};
	Param<bool> display_hdr {
	  "display.hdr",
	  true,
	  {.name = "HDR output", .description = "Show HDR when the display has it turned on in the system", .requires_restart = true}
	};
	Param<int64_t> quality_ray_tracing {
	  "quality.ray_tracing",
	  0,
	  {.name = "Hardware ray tracing",
		  .kind = Kind::advanced,
		  .description = "Auto uses it only on GPUs built for it. Off keeps the raster paths everywhere",
		  .options = options::quality_ray_tracing,
		  .requires_restart = true}
	};
	Param<bool> voxels_trace_ray_query {
	  "voxels.trace_ray_query",
	  true,
	  {.name = "Ray query voxel rays",
		  .kind = Kind::advanced,
		  .description = "Marched voxel rays use hardware ray queries when the GPU has them",
		  .requires_restart = true}
	};
	Param<bool> gi_enabled {
	  "gi.enabled", true, {.name = "Radiance cascades", .description = "World space radiance probes. Custom preset only"}
	};
	Param<int64_t> gi_quality {
	  "gi.quality",
	  2,
	  {.name = "Quality",
		  .description = "Rays, cascades, probe spacing and update rate together. Custom uses the ones below",
		  .options = options::gi_quality}
	};
	Param<int64_t> gi_update_interval {
	  "gi.update_interval",
	  1,
	  {.name = "Update interval",
		  .min = 1,
		  .max = 4,
		  .unit = "frames",
		  .description = "Frames between GI updates. Custom quality only"}
	};
	Param<int64_t> gi_secondary_points {
	  "gi.secondary_points",
	  65536,
	  {.name = "Multibounce points",
		  .min = 1024,
		  .max = 65536,
		  .kind = Kind::advanced,
		  .description = "Off screen hits kept per frame for the second bounce. Custom quality only"}
	};
	Param<bool> gi_multibounce {
	  "gi.multibounce",
	  true,
	  {.name = "Multibounce", .description = "Light keeps bouncing off screen, for about half again the rays. Custom quality only"}
	};
	Param<double> gi_probe_spacing {
	  "gi.probe_spacing",
	  0.1,
	  {.name = "Probe spacing",
		  .min = 0.025,
		  .max = 1.0,
		  .unit = "m",
		  .description = "Metres between the nearest probes. Doubles with every cascade. Custom quality only"}
	};
	Param<double> gi_lod_distance {
	  "gi.lod_distance",
	  4.0,
	  {.name = "LOD distance",
		  .min = 0.5,
		  .max = 64.0,
		  .unit = "m",
		  .description = "Distance where probe spacing starts doubling. Custom quality only"}
	};
	Param<int64_t> gi_cascades {
	  "gi.cascades",
	  5,
	  {.name = "Cascades",
		  .min = 1,
		  .max = 6,
		  .description = "Each reaches four times as far as the one below. Custom quality only"}
	};
	Param<int64_t> gi_ray_resolution {
	  "gi.ray_resolution",
	  1,
	  {.name = "Ray resolution",
		  .description = "Pixels that place probes and trace rays. Custom quality only",
		  .options = options::gi_ray_resolution}
	};
	Param<int64_t> gi_probe_lifetime {
	  "gi.probe_lifetime",
	  60,
	  {.name = "Probe lifetime",
		  .min = 1,
		  .max = 600,
		  .unit = "frames",
		  .kind = Kind::advanced,
		  .description = "Frames a probe lives after nothing needs it"}
	};
	Param<double> gi_history_seconds {
	  "gi.history_seconds",
	  0.3,
	  {.name = "History length",
		  .min = 0.0,
		  .max = 5.0,
		  .unit = "s",
		  .description =
		     "Seconds of light each probe direction averages. 0 keeps none. Longer is smoother and slower to follow lights"}
	};
	Param<bool> gi_history_per_frame {
	  "gi.history_per_frame",
	  false,
	  {.name = "Per frame history",
		  .kind = Kind::advanced,
		  .description = "Blend a fixed share of each frame instead of a length in seconds"}
	};
	Param<double> gi_history {
	  "gi.history",
	  0.03,
	  {.name = "History blend",
		  .min = 0.005,
		  .max = 1.0,
		  .description = "Share of each new frame blended in. Lower is smoother and slower to follow lights"}
	};
	Param<double> gi_motion_threshold {
	  "gi.motion_threshold",
	  0.01,
	  {.name = "Motion threshold",
		  .min = 0.0,
		  .max = 0.2,
		  .unit = "m",
		  .description = "How far a voxel body moves in a frame before nearby probes forget their history"}
	};
	Param<double> gi_max_ray_radiance {
	  "gi.max_ray_radiance",
	  16.0,
	  {.name = "Ray brightness clamp",
		  .min = 0.5,
		  .max = 1000.0,
		  .kind = Kind::advanced,
		  .description = "Brightest a single ray may count. Lower is steadier"}
	};
	Param<double> gi_intensity {
	  "gi.intensity",
	  2.0,
	  {.name = "Bounce intensity", .min = 0.0, .max = 8.0, .description = "Strength of the bounced light. 1 is physical"}
	};
	Param<int64_t> gi_debug_view {
	  "gi.debug_view",
	  0,
	  {.name = "Probe view",
		  .kind = Kind::debug,
		  .description = "Draws the probe cells of one cascade over the scene",
		  .options = options::gi_debug_view}
	};
	Param<int64_t> gi_debug_mode {
	  "gi.debug_mode",
	  0,
	  {.name = "Probe view shows",
		  .kind = Kind::debug,
		  .description = "What the probe view colors cells by",
		  .options = options::gi_debug_mode}
	};
	Param<int64_t> gi_backend {
	  "gi.backend",
	  0,
	  {.name = "Backend",
		  .kind = Kind::advanced,
		  .description = "Radiance clipmap is unfinished: it only fills its voxel clipmap so far",
		  .options = options::gi_backend}
	};
	Param<double> gi_clip_cell {
	  "gi.clip_cell",
	  0.2,
	  {.name = "Clipmap cell",
		  .min = 0.1,
		  .max = 1.0,
		  .unit = "m",
		  .description = "Metres per cell of level 0. Every level doubles it"}
	};
	Param<int64_t> gi_clip_resolution {
	  "gi.clip_resolution",
	  1,
	  {.name = "Clipmap resolution",
		  .description = "Cells per side of each level. Custom quality only",
		  .options = options::gi_clip_resolution}
	};
	Param<int64_t> gi_clip_levels {
	  "gi.clip_levels",
	  4,
	  {.name = "Clipmap levels", .min = 1, .max = 6, .description = "Levels around the camera. Custom quality only"}
	};
	Param<int64_t> gi_clip_debug_view {
	  "gi.clip_debug_view",
	  0,
	  {.name = "Clipmap view",
		  .kind = Kind::debug,
		  .description = "Draws one clipmap level's cells instead of the scene",
		  .options = options::gi_clip_debug_view}
	};
	Param<int64_t> gi_clip_debug_mode {
	  "gi.clip_debug_mode",
	  0,
	  {.name = "Clipmap view shows",
		  .kind = Kind::debug,
		  .description = "What the clipmap view draws per cell",
		  .options = options::gi_clip_debug_mode}
	};
};

[[nodiscard]]
TOAST_API auto rendererParams() -> RendererParams&;

}
