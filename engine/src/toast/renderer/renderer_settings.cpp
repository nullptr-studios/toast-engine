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
}

void registerVoxels(VulkanRenderer& renderer) {
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
