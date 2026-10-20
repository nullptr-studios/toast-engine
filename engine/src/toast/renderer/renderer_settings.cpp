#include "renderer_settings.hpp"

#include "passes/environment_pass.hpp"
#include "shadow_constants.hpp"
#include "shadow_slots.hpp"
#include "vulkan_renderer.hpp"

#include <algorithm>
#include <toast/settings/settings.hpp>
#include <utility>

namespace renderer {
namespace {

using toast::settings::Meta;

void registerDisplay(VulkanRenderer& renderer) {
	toast::settings::declareBool(
	    "renderer.display.clamp_to_simulation",
	    true,
	    {.label = "Clamp to game thread",
			 .category = "Display",
			 .description = "On (default): the render thread waits for each new simulation frame instead of "
			                "presenting it more than once, so it never draws faster than the game thread ticks. "
			                "Off: the render thread never waits, repeating the last frame as fast as the GPU "
			                "allows (100% GPU usage) between simulation updates."}
	)
	    .onChange([&renderer](bool clamp) { renderer.setClampToSimulation(clamp); });

	toast::settings::declareFloat(
	    "renderer.display.frame_rate_limit",
	    0.0,
	    {.label = "Frame rate limit",
			 .category = "Display",
			 .description = "Hz the render thread draws and presents at. 0 runs uncapped. Applies in either mode.",
			 .min = 0.0,
			 .max = 360.0,
			 .step = 1.0}
	)
	    .onChange([&renderer](double v) { renderer.setFrameRateLimit(v); });
}

void registerQuality(VulkanRenderer& renderer) {
	const auto pass_toggle = [&renderer](std::string_view key, std::string_view pass, bool default_value, std::string description) {
		toast::settings::declareBool(
		    key, default_value, {.label = std::string {pass}, .category = "Quality", .description = std::move(description)}
		)
		    .onChange([&renderer, pass](bool enabled) { renderer.setPostProcessPassEnabled(pass, enabled); });
	};

	pass_toggle("renderer.quality.bloom", "Bloom", true, "Glow around highlights bright enough to overwhelm a lens.");
	pass_toggle("renderer.quality.ssr", "SSR", true, "Screen-space reflections. The most expensive post pass.");
	pass_toggle("renderer.quality.ssao", "SSAO", true, "Contact darkening where geometry meets geometry.");
	pass_toggle("renderer.quality.fxaa", "FXAA", true, "Edge anti-aliasing on the tonemapped image. Skipped while TAA is on.");

	toast::settings::declareBool(
	    "renderer.quality.taa",
	    true,
	    {.label = "TAA",
			 .category = "Quality",
			 .description = "Temporal anti-aliasing. Jitters the camera and resolves across frames in place of FXAA."}
	)
	    .onChange([&renderer](bool enabled) { renderer.setTaaEnabled(enabled); });

	toast::settings::declareFloat(
	    "renderer.quality.taa_history",
	    0.9,
	    {.label = "TAA history",
			 .category = "Quality",
			 .description = "Share of the previous frame each pixel keeps. Higher is smoother and ghosts more.",
			 .min = 0.5,
			 .max = 0.98,
			 .step = 0.01}
	)
	    .onChange([&renderer](double v) { renderer.setTaaHistoryWeight(static_cast<float>(v)); });

	toast::settings::declareBool(
	    "renderer.quality.traced_shadows",
	    false,
	    {.label = "Traced shadows",
			 .category = "Quality",
			 .description = "Trace shadow rays against the acceleration structure instead of sampling the shadow "
			                "maps. No effect on a device without ray query."}
	)
	    .onChange([&renderer](bool enabled) { renderer.setTracedShadowsEnabled(enabled); });
}

void registerShadows() {
	const auto resolutions = std::vector<std::string> {"256", "512", "1024", "2048", "4096"};

	toast::settings::declareInt(
	    "renderer.shadows.cascade_resolution",
	    2,
	    {.label = "Cascade resolution",
			 .category = "Shadows",
			 .description = "Per-cascade square resolution for directional shadows.",
			 .options = resolutions,
			 .requires_restart = true}
	)
	    .onChange([resolutions](int64_t index) {
		    if (index >= 0 && std::cmp_less(index, resolutions.size())) {
			    shadows::setCascadeResolution(static_cast<uint32_t>(std::stoul(resolutions[index])));
		    }
	    });

	toast::settings::declareInt(
	    "renderer.shadows.punctual_resolution",
	    1,
	    {.label = "Punctual resolution",
			 .category = "Shadows",
			 .description = "Full-size layer resolution for spot and point light shadows.",
			 .options = resolutions,
			 .requires_restart = true}
	)
	    .onChange([resolutions](int64_t index) {
		    if (index >= 0 && std::cmp_less(index, resolutions.size())) {
			    shadows::setPunctualResolution(static_cast<uint32_t>(std::stoul(resolutions[index])));
		    }
	    });

	toast::settings::declareFloat(
	    "renderer.shadows.distance",
	    shadows::k_shadow_distance,
	    {.label = "Shadow distance",
			 .category = "Shadows",
			 .description = "How far from the camera directional shadows are fitted, in "
			                "metres. The cascades cover this range, so lowering it buys "
			                "sharpness rather than performance.",
			 .min = 10.0,
			 .max = 1000.0,
			 .step = 5.0}
	)
	    .onChange([](double v) { shadows::setShadowDistance(static_cast<float>(v)); });

	toast::settings::declareFloat(
	    "renderer.shadows.slot_retention",
	    shadow_slots::k_default_retention,
	    {.label = "Slot retention",
			 .category = "Shadows",
			 .description = "How many times more important than the weakest shadowed light another light must be to "
			                "take its shadow slot. Higher keeps cached shadow maps longer.",
			 .min = 1.0,
			 .max = 4.0,
			 .step = 0.05}
	)
	    .onChange([](double v) { shadow_slots::setRetention(static_cast<float>(v)); });

	toast::settings::declareInt(
	    "renderer.shadows.slot_grace",
	    shadow_slots::k_default_grace_frames,
	    {.label = "Slot grace",
			 .category = "Shadows",
			 .description = "Frames a shadowed light keeps its slot after leaving the view while another light wants "
			                "it. Higher re-renders less when looking around, lower favours what is on screen.",
			 .min = 0.0,
			 .max = 300.0,
			 .step = 1.0}
	)
	    .onChange([](int64_t frames) { shadow_slots::setGraceFrames(static_cast<uint32_t>(std::max<int64_t>(frames, 0))); });

	toast::settings::declareInt(
	    "renderer.shadows.spot_slots",
	    shadows::k_default_spot_shadows,
	    {.label = "Spot shadow slots",
			 .category = "Shadows",
			 .description = "Spot lights that can hold a shadow. Each slot is one punctual layer in every copy of the map, "
			                "about 3 MB at 512",
			 .min = 0.0,
			 .max = static_cast<double>(shadows::k_max_spot_shadows),
			 .step = 1.0,
			 .requires_restart = true}
	)
	    .onChange([](int64_t slots) { shadows::setSpotShadowSlots(static_cast<uint32_t>(std::max<int64_t>(slots, 0))); });

	toast::settings::declareInt(
	    "renderer.shadows.point_slots",
	    shadows::k_default_point_shadows,
	    {.label = "Point shadow slots",
			 .category = "Shadows",
			 .description = "Point lights that can hold a shadow. Each slot is six punctual layers in every copy of the map, "
			                "about 18 MB at 512",
			 .min = 0.0,
			 .max = static_cast<double>(shadows::k_max_point_shadows),
			 .step = 1.0,
			 .requires_restart = true}
	)
	    .onChange([](int64_t slots) { shadows::setPointShadowSlots(static_cast<uint32_t>(std::max<int64_t>(slots, 0))); });

	toast::settings::declareInt(
	    "renderer.shadows.spot_budget",
	    shadows::k_max_spot_shadows,
	    {.label = "Spot lights casting",
			 .category = "Shadows",
			 .description = "Of the spot shadow slots how many may be used at once, nearest lights first. Lowers live "
			                "without a restart to compare costs",
			 .min = 0.0,
			 .max = static_cast<double>(shadows::k_max_spot_shadows),
			 .step = 1.0}
	)
	    .onChange([](int64_t lights) { shadows::setSpotShadowBudget(static_cast<uint32_t>(std::max<int64_t>(lights, 0))); });

	toast::settings::declareInt(
	    "renderer.shadows.point_budget",
	    shadows::k_max_point_shadows,
	    {.label = "Point lights casting",
			 .category = "Shadows",
			 .description = "Of the point shadow slots how many may be used at once, nearest lights first. Each costs six "
			                "shadow faces whenever its casters change",
			 .min = 0.0,
			 .max = static_cast<double>(shadows::k_max_point_shadows),
			 .step = 1.0}
	)
	    .onChange([](int64_t lights) { shadows::setPointShadowBudget(static_cast<uint32_t>(std::max<int64_t>(lights, 0))); });

	toast::settings::declareFloat(
	    "renderer.shadows.full_resolution_ranges",
	    shadows::k_punctual_full_resolution_ranges,
	    {.label = "Full resolution reach",
			 .category = "Shadows",
			 .description = "Camera distance in light ranges within which a spot or point shadow keeps its full "
			                "resolution. Past it each doubling of the distance halves the shadow map",
			 .min = 0.5,
			 .max = 16.0,
			 .step = 0.5}
	)
	    .onChange([](double ranges) { shadows::setPunctualFullResolutionRanges(static_cast<float>(ranges)); });

	toast::settings::declareFloat(
	    "renderer.shadows.cascade_split_lambda",
	    shadows::k_cascade_split_lambda,
	    {.label = "Cascade split",
			 .category = "Shadows",
			 .description = "0 spaces the cascades evenly and 1 logarithmically. Higher keeps more resolution near the "
			                "camera",
			 .min = 0.0,
			 .max = 1.0,
			 .step = 0.05}
	)
	    .onChange([](double lambda) { shadows::setCascadeSplitLambda(static_cast<float>(lambda)); });

	toast::settings::declareFloat(
	    "renderer.shadows.cascade_padding",
	    shadows::k_cascade_padding,
	    {.label = "Cascade padding",
			 .category = "Shadows",
			 .description = "Extra share of its slice each cascade covers so it keeps its cached shadow map while the "
			                "camera moves inside it. Higher re-renders less and blurs a little more",
			 .min = 0.0,
			 .max = 0.5,
			 .step = 0.01}
	)
	    .onChange([](double padding) { shadows::setCascadePadding(static_cast<float>(padding)); });

	toast::settings::declareFloat(
	    "renderer.shadows.cascade_receiver_depth",
	    shadows::k_cascade_receiver_depth_radii,
	    {.label = "Cascade receiver depth",
			 .category = "Shadows",
			 .description = "How far along the light each cascade reaches in cascade radii. Lower draws fewer casters "
			                "and loses shadows on surfaces far down the light",
			 .min = 1.0,
			 .max = 16.0,
			 .step = 0.5}
	)
	    .onChange([](double radii) { shadows::setCascadeReceiverDepthRadii(static_cast<float>(radii)); });

	toast::settings::declareBool(
	    "renderer.shadows.static_cache",
	    true,
	    {.label = "Static caster cache",
			 .category = "Shadows",
			 .description = "Resting voxel volumes render into a cached map that each shadow copies under the moving "
			                "casters. Off draws every voxel caster whenever anything in that shadow changes"}
	)
	    .onChange([](bool enabled) { shadows::setStaticCasterCache(enabled); });

	toast::settings::declareFloat(
	    "renderer.shadows.caster_tolerance",
	    shadows::k_caster_tolerance,
	    {.label = "Caster tolerance",
			 .category = "Shadows",
			 .description = "Meters a voxel volume may drift before its shadow follows. Higher keeps jittering bodies "
			                "in the static cache, lower makes slow shadows step less",
			 .min = 0.0,
			 .max = 0.1,
			 .step = 0.005}
	)
	    .onChange([](double meters) { shadows::setCasterTolerance(static_cast<float>(meters)); });

	toast::settings::declareBool(
	    "renderer.shadows.voxel_mesh_shaders",
	    true,
	    {.label = "Mesh shader voxel casters",
			 .category = "Shadows",
			 .description = "Voxel casters rasterize task and mesh shader quads on GPUs that have them. Off marches "
			                "every voxel caster per shadow texel"}
	)
	    .onChange([](bool enabled) { shadows::setVoxelMeshShadows(enabled); });
}

void registerVoxels(VulkanRenderer& renderer) {
	toast::settings::declareInt(
	    "renderer.voxels.light_candidates",
	    8,
	    {.label = "Light candidates",
			 .category = "Voxels",
			 .description = "Lights in Sampled voxel shadow mode share one marched ray per pixel chosen among this many "
			                "candidates from the froxel. More is less noise and more shading work",
			 .min = 1.0,
			 .max = 32.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVoxelLightCandidates(static_cast<uint32_t>(std::max<int64_t>(v, 1))); });

	toast::settings::declareBool(
	    "renderer.voxels.ambient_rays",
	    true,
	    {.label = "Ambient rays",
			 .category = "Voxels",
			 .description = "Two super-sparse rays per voxel pixel occlude ambient light by how far they get, in place of "
			                "SSAO. Off keeps SSAO on voxels"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelAmbientRays(enabled); });

	toast::settings::declareFloat(
	    "renderer.voxels.ambient_reach",
	    2.4,
	    {.label = "Ambient reach",
			 .category = "Voxels",
			 .description = "Meters an ambient ray travels before it counts as open sky. Teardown clamps at 24 voxels",
			 .min = 0.2,
			 .max = 20.0,
			 .step = 0.1}
	)
	    .onChange([&renderer](double v) { renderer.setVoxelAmbientReach(static_cast<float>(v)); });

	toast::settings::declareBool(
	    "renderer.voxels.glass_refraction_enabled",
	    true,
	    {.label = "Glass refraction",
			 .category = "Voxels",
			 .description = "Glass bends what is behind it by its IOR. Costs a copy of the scene colour and depth plus a "
			                "march through the volume for every glass pixel. Off draws glass straight through"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelGlassRefractionEnabled(enabled); });

	toast::settings::declareFloat(
	    "renderer.voxels.glass_refraction",
	    1.0,
	    {.label = "Glass refraction strength",
			 .category = "Voxels",
			 .description = "How far glass bends what is behind it. 1 follows the IOR and higher exaggerates it",
			 .min = 0.0,
			 .max = 4.0,
			 .step = 0.05}
	)
	    .onChange([&renderer](double v) { renderer.setVoxelGlassRefraction(static_cast<float>(v)); });

	toast::settings::declareFloat(
	    "renderer.voxels.glass_refraction_distance",
	    60.0,
	    {.label = "Glass refraction distance",
			 .category = "Voxels",
			 .description = "Meters from the camera past which glass draws straight through. Far glass bends too little to "
			                "see and the scene copy is skipped when every glass brick is past it",
			 .min = 1.0,
			 .max = 500.0,
			 .step = 1.0}
	)
	    .onChange([&renderer](double v) { renderer.setVoxelGlassRefractionDistance(static_cast<float>(v)); });

	toast::settings::declareInt(
	    "renderer.voxels.glass_refraction_depth",
	    32,
	    {.label = "Glass refraction depth",
			 .category = "Voxels",
			 .description = "Voxels refraction follows through thick glass to find its far side. Past it the far side "
			                "counts as parallel to the near one",
			 .min = 1.0,
			 .max = 96.0}
	)
	    .onChange([&renderer](int64_t v) {
		    renderer.setVoxelGlassRefractionVoxels(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 96)));
	    });

	toast::settings::declareInt(
	    "renderer.voxels.glass_lit_surfaces",
	    4,
	    {.label = "Glass lit surfaces",
			 .category = "Voxels",
			 .description = "Glass surfaces per brick and pixel that get full lighting and reflections. Layers past it "
			                "only tint what is behind them",
			 .min = 1.0,
			 .max = 4.0}
	)
	    .onChange([&renderer](int64_t v) {
		    renderer.setVoxelGlassLitSurfaces(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 4)));
	    });

	toast::settings::declareBool(
	    "renderer.voxels.glass_shadows",
	    true,
	    {.label = "Glass shadows",
			 .category = "Voxels",
			 .description = "Glass tints the light that reaches voxels behind it in every light mode. Off makes glass cast "
			                "no shadow and lets marched shadow rays take coarse steps through it"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelGlassShadows(enabled); });

	toast::settings::declareInt(
	    "renderer.voxels.glass_shadow_reach",
	    50,
	    {.label = "Glass shadow reach",
			 .category = "Voxels",
			 .description = "Meters a sun looks for glass between a voxel and the sky when its shadow comes from the "
			                "shadow map",
			 .min = 1.0,
			 .max = 2000.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVoxelGlassShadowReach(static_cast<uint32_t>(std::max<int64_t>(v, 1))); });

	toast::settings::declareInt(
	    "renderer.voxels.sun_reach",
	    200,
	    {.label = "Sun ray reach",
			 .category = "Voxels",
			 .description = "Meters a marched sun shadow ray travels through voxels. Longer finds distant occluders and "
			                "costs more on open ground",
			 .min = 10.0,
			 .max = 2000.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVoxelSunReach(static_cast<uint32_t>(std::max<int64_t>(v, 1))); });

	toast::settings::declareInt(
	    "renderer.voxels.shadow_instances",
	    256,
	    {.label = "Shadow ray instances",
			 .category = "Voxels",
			 .description = "Nearest drawn voxel volumes a marched shadow ray tests against. Farther volumes cast no "
			                "marched shadow",
			 .min = 1.0,
			 .max = 4096.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVoxelShadowInstances(static_cast<uint32_t>(std::max<int64_t>(v, 1))); });

	toast::settings::declareFloat(
	    "renderer.voxels.edge_rounding",
	    0.3,
	    {.label = "Edge rounding",
			 .category = "Voxels",
			 .description = "Width in voxels that normals blend across edges near the camera. 0 keeps flat faces.",
			 .min = 0.0,
			 .max = 1.0,
			 .step = 0.05}
	)
	    .onChange([&renderer](double v) { renderer.setVoxelNormalRounding(static_cast<float>(v)); });

	toast::settings::declareBool(
	    "renderer.voxels.cell_boxes",
	    true,
	    {.label = "Cell boxes",
			 .category = "Voxels",
			 .description = "On draws one box per non-empty 32 voxel cell front to back and marches only inside it. Off "
			                "draws one box per volume and marches the whole volume, kept to compare the two."}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelCellBoxes(enabled); });

	toast::settings::declareBool(
	    "renderer.voxels.occlusion",
	    true,
	    {.label = "Occlusion culling",
			 .category = "Voxels",
			 .description = "Skips cells hidden behind closer geometry using a depth pyramid. Needs cell boxes"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelOcclusion(enabled); });

	toast::settings::declareBool(
	    "renderer.voxels.mesh_shaders",
	    true,
	    {.label = "Mesh shaders",
			 .category = "Voxels",
			 .description = "Meshes near voxels in task and mesh shaders on GPUs that have them. Off uses the compute mesher, "
			                "which every GPU runs"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelMeshShaders(enabled); });

	toast::settings::declareFloat(
	    "renderer.voxels.raster_pixels",
	    4.0,
	    {.label = "Raster voxel size",
			 .category = "Voxels",
			 .description = "Cells whose voxels cover at least this many pixels are meshed into quads and rasterized",
			 .min = 0.0,
			 .max = 32.0,
			 .step = 0.5}
	)
	    .onChange([&renderer](double pixels) { renderer.setVoxelRasterPixels(static_cast<float>(pixels)); });

	toast::settings::declareFloat(
	    "renderer.voxels.occlusion_min_hidden",
	    0.05,
	    {.label = "Occlusion idle below",
			 .category = "Voxels",
			 .description = "Share of cells occlusion culling must hide to keep running. Below it for a few frames it "
			                "pauses since it costs more than it saves. 0 never pauses it",
			 .min = 0.0,
			 .max = 0.5,
			 .step = 0.01}
	)
	    .onChange([&renderer](double share) { renderer.setVoxelOcclusionMinHidden(static_cast<float>(share)); });

	toast::settings::declareInt(
	    "renderer.voxels.occlusion_idle_frames",
	    120,
	    {.label = "Occlusion idle frames",
			 .category = "Voxels",
			 .description = "Frames occlusion culling stays paused before it measures again",
			 .min = 0.0,
			 .max = 600.0,
			 .step = 1.0}
	)
	    .onChange([&renderer](int64_t frames) {
		    renderer.setVoxelOcclusionIdleFrames(static_cast<uint32_t>(std::max<int64_t>(frames, 0)));
	    });
}

void registerEnvironment(VulkanRenderer& renderer) {
	toast::settings::declareFloat(
	    "renderer.environment.sky_intensity",
	    1.0,
	    {.label = "Sky intensity",
			 .category = "Environment",
			 .description = "Brightness of the generated sky, folded into the environment maps. Changing it re-runs "
			                "the precompute on the next frame.",
			 .min = 0.0,
			 .max = 20.0}
	)
	    .onChange([&renderer](double v) {
		    if (auto* pass = renderer.getEnvironmentPassMutable()) {
			    pass->setSkyIntensity(static_cast<float>(v));
		    }
	    });
}

}

void registerRendererStartupSettings() {
	registerShadows();
}

void registerRendererSettings(VulkanRenderer& renderer) {
	registerDisplay(renderer);
	registerQuality(renderer);
	registerVoxels(renderer);
	registerEnvironment(renderer);
}

}
