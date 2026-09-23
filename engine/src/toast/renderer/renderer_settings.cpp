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
	pass_toggle("renderer.quality.fxaa", "FXAA", true, "Edge anti-aliasing on the tonemapped image.");

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
	registerEnvironment(renderer);
}

}
