#include "renderer_settings.hpp"

#include "passes/environment_pass.hpp"
#include "shader_compiler.hpp"
#include "shadow_constants.hpp"
#include "shadow_slots.hpp"
#include "vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <toast/log.hpp>
#include <toast/settings/settings.hpp>
#include <utility>

namespace renderer {
namespace {

using toast::settings::Meta;

constexpr uint32_t k_tiers_per_branch = 4;
constexpr uint32_t k_preset_custom = 2 * k_tiers_per_branch;

struct PresetKnobs {
	bool ssr = true;
	bool ssao = true;
	bool traced_specular = true;
	float specular_roughness = 0.6f;
	float specular_reach = 32.0f;
	uint32_t light_candidates = 8;
	bool ambient_rays = true;
	bool glass_shadows = true;
	uint32_t volumetric_quality = 2;
	uint32_t gi_quality = 2;
	bool gi_enabled = true;
	float render_scale = 1.0f;
	float shadow_distance = shadows::k_shadow_distance;
	uint32_t spot_budget = shadows::k_max_spot_shadows;
	uint32_t point_budget = shadows::k_max_point_shadows;
	bool shadow_maps_only = false;
	/// Zero steps keeps the post process volume values
	float ssr_reach = 0.0f;
	uint32_t ssr_steps = 0;
	bool motion_blur = false;
};

/// Low Medium High Epic without ray tracing, then Low RT Medium RT High RT Epic RT
constexpr std::array<PresetKnobs, k_preset_custom> k_preset_tiers {
  {
   // Without ray tracing
    {.ssr = false,
		 .ssao = false,
		 .traced_specular = false,
		 .specular_roughness = 0.3f,
		 .specular_reach = 16.0f,
		 .light_candidates = 2,
		 .ambient_rays = false,
		 .glass_shadows = false,
		 .volumetric_quality = 0,
		 .gi_quality = 0,
		 .gi_enabled = false,
		 .render_scale = 0.65f,
		 .shadow_distance = 60.0f,
		 .spot_budget = 4,
		 .point_budget = 2,
		 .shadow_maps_only = true},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.4f,
	   .specular_reach = 24.0f,
	   .light_candidates = 4,
	   .ambient_rays = false,
	   .glass_shadows = false,
	   .volumetric_quality = 0,
	   .gi_quality = 0,
	   .gi_enabled = false,
	   .render_scale = 0.75f,
	   .shadow_distance = 100.0f,
	   .spot_budget = 6,
	   .point_budget = 3,
	   .shadow_maps_only = true,
	   .ssr_reach = 12.0f,
	   .ssr_steps = 48},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.5f,
	   .specular_reach = 32.0f,
	   .light_candidates = 6,
	   .ambient_rays = false,
	   .glass_shadows = true,
	   .volumetric_quality = 1,
	   .gi_quality = 0,
	   .gi_enabled = true,
	   .render_scale = 0.85f,
	   .shadow_distance = 150.0f,
	   .spot_budget = 10,
	   .point_budget = 5,
	   .shadow_maps_only = true,
	   .ssr_reach = 20.0f,
	   .ssr_steps = 64,
	   .motion_blur = true},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.5f,
	   .specular_reach = 32.0f,
	   .light_candidates = 8,
	   .ambient_rays = false,
	   .glass_shadows = true,
	   .volumetric_quality = 1,
	   .gi_quality = 1,
	   .gi_enabled = true,
	   .render_scale = 1.0f,
	   .shadow_distance = 250.0f,
	   .spot_budget = 16,
	   .point_budget = 8,
	   .shadow_maps_only = true,
	   .ssr_reach = 32.0f,
	   .ssr_steps = 96,
	   .motion_blur = true},
   // With ray tracing
    {.ssr = false,
		 .ssao = false,
		 .traced_specular = false,
		 .specular_roughness = 0.3f,
		 .specular_reach = 16.0f,
		 .light_candidates = 2,
		 .ambient_rays = false,
		 .glass_shadows = false,
		 .volumetric_quality = 0,
		 .gi_quality = 0,
		 .shadow_distance = 60.0f,
		 .spot_budget = 4,
		 .point_budget = 2},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.4f,
	   .specular_reach = 24.0f,
	   .light_candidates = 4,
	   .ambient_rays = true,
	   .glass_shadows = true,
	   .volumetric_quality = 1,
	   .gi_quality = 1,
	   .shadow_distance = 100.0f,
	   .spot_budget = 8,
	   .point_budget = 4},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = true,
	   .specular_roughness = 0.5f,
	   .specular_reach = 32.0f,
	   .light_candidates = 8,
	   .ambient_rays = true,
	   .glass_shadows = true,
	   .volumetric_quality = 2,
	   .gi_quality = 2,
	   .shadow_distance = 150.0f,
	   .spot_budget = 16,
	   .point_budget = 8,
	   .motion_blur = true},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = true,
	   .specular_roughness = 0.6f,
	   .specular_reach = 48.0f,
	   .light_candidates = 16,
	   .ambient_rays = true,
	   .glass_shadows = true,
	   .volumetric_quality = 3,
	   .gi_quality = 3,
	   .shadow_distance = 250.0f,
	   .spot_budget = shadows::k_max_spot_shadows,
	   .point_budget = shadows::k_max_point_shadows,
	   .motion_blur = true},
   }
};

/// Main thread writes and the debug window reads under the mutex
struct PresetState {
	std::mutex mutex;
	VulkanRenderer* renderer = nullptr;
	uint32_t preset = k_preset_custom;
	/// Off until every driven setting has reported its value
	bool ready = false;
	PresetKnobs knobs;
};

PresetState g_preset;

/// RT tiers fall back to the non RT tier of the same level when not tracing
auto activeKnobs() -> const PresetKnobs& {
	if (g_preset.preset >= k_preset_custom) {
		return g_preset.knobs;
	}
	uint32_t tier = g_preset.preset;
	if (tier >= k_tiers_per_branch && !g_preset.renderer->getCore().isRayTracingSupported()) {
		tier -= k_tiers_per_branch;
	}
	return k_preset_tiers[tier];
}

void applyPreset() {
	if (!g_preset.ready) {
		return;
	}
	VulkanRenderer& renderer = *g_preset.renderer;
	const PresetKnobs& knobs = activeKnobs();
	renderer.setRenderScale(knobs.render_scale);
	renderer.giSettings().enabled.store(knobs.gi_enabled, std::memory_order_relaxed);
	renderer.setPostProcessPassEnabled("SSR", knobs.ssr);
	renderer.setSsrOverride(knobs.ssr_reach, knobs.ssr_steps);
	renderer.setPostProcessPassEnabled("Motion blur", knobs.motion_blur);
	renderer.setVoxelShadowMapsOnly(knobs.shadow_maps_only);
	renderer.setPostProcessPassEnabled("SSAO", knobs.ssao);
	renderer.setVoxelTracedSpecular(knobs.traced_specular);
	renderer.setVoxelSpecularRoughness(knobs.specular_roughness);
	renderer.setVoxelSpecularReach(knobs.specular_reach);
	renderer.setVoxelLightCandidates(knobs.light_candidates);
	renderer.setVoxelAmbientRays(knobs.ambient_rays);
	renderer.setVoxelGlassShadows(knobs.glass_shadows);
	renderer.setVolumetricQuality(knobs.volumetric_quality);
	renderer.giSettings().quality.store(knobs.gi_quality, std::memory_order_relaxed);
	shadows::setShadowDistance(knobs.shadow_distance);
	shadows::setSpotShadowBudget(knobs.spot_budget);
	shadows::setPointShadowBudget(knobs.point_budget);
}

using toast::settings::Value;

struct DrivenSetting {
	std::string_view key;
	Value (*get)(const PresetKnobs&);
};

const std::array<DrivenSetting, 19> k_driven {
  {
   {"renderer.voxels.shadow_maps_only", [](const PresetKnobs& k) { return Value {k.shadow_maps_only}; }},
   {"renderer.quality.ssr_reach", [](const PresetKnobs& k) { return Value {static_cast<double>(k.ssr_reach)}; }},
   {"renderer.quality.ssr_steps", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.ssr_steps)}; }},
   {"renderer.quality.motion_blur", [](const PresetKnobs& k) { return Value {k.motion_blur}; }},
   {"renderer.quality.ssr", [](const PresetKnobs& k) { return Value {k.ssr}; }},
   {"renderer.quality.ssao", [](const PresetKnobs& k) { return Value {k.ssao}; }},
   {"renderer.quality.render_scale", [](const PresetKnobs& k) { return Value {static_cast<double>(k.render_scale)}; }},
   {"renderer.voxels.traced_specular", [](const PresetKnobs& k) { return Value {k.traced_specular}; }},
   {"renderer.voxels.specular_roughness",
	   [](const PresetKnobs& k) { return Value {static_cast<double>(k.specular_roughness)}; }},
   {"renderer.voxels.specular_reach", [](const PresetKnobs& k) { return Value {static_cast<double>(k.specular_reach)}; }},
   {"renderer.voxels.light_candidates", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.light_candidates)}; }},
   {"renderer.voxels.ambient_rays", [](const PresetKnobs& k) { return Value {k.ambient_rays}; }},
   {"renderer.voxels.glass_shadows", [](const PresetKnobs& k) { return Value {k.glass_shadows}; }},
   {"renderer.volumetrics.quality", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.volumetric_quality)}; }},
   {"renderer.gi.enabled", [](const PresetKnobs& k) { return Value {k.gi_enabled}; }},
   {"renderer.gi.quality", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.gi_quality)}; }},
   {"renderer.shadows.distance", [](const PresetKnobs& k) { return Value {static_cast<double>(k.shadow_distance)}; }},
   {"renderer.shadows.spot_budget", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.spot_budget)}; }},
   {"renderer.shadows.point_budget", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.point_budget)}; }},
   }
};

/// Custom starts from the tier it leaves
void seedSettingsFrom(const PresetKnobs& tier) {
	auto& settings = toast::settings::Settings::get();
	for (const DrivenSetting& driven : k_driven) {
		settings.setValue(driven.key, driven.get(tier), true);
	}
}

template<typename T, typename V, typename Convert>
auto presetKnob(T PresetKnobs::* knob, Convert convert) {
	return [knob, convert](V value) {
		{
			const std::lock_guard lock(g_preset.mutex);
			g_preset.knobs.*knob = convert(value);
		}
		applyPreset();
	};
}

constexpr auto k_same_bool = [](bool v) { return v; };
constexpr auto k_to_float = [](double v) { return static_cast<float>(v); };
constexpr auto k_to_count = [](int64_t v) { return static_cast<uint32_t>(std::max<int64_t>(v, 0)); };

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

	static float hdr_paper_white = 0.0f;
	static float hdr_peak = 0.0f;
	toast::settings::declareFloat(
	    "renderer.display.hdr_paper_white",
	    0.0,
	    {.label = "HDR paper white",
			 .category = "Display",
			 .description = "Nits that SDR white and the UI sit at when HDR output is on. 0 follows the Windows SDR content "
			                "brightness slider",
			 .min = 0.0,
			 .max = 500.0,
			 .step = 5.0}
	)
	    .onChange([&renderer](double v) {
		    hdr_paper_white = static_cast<float>(v);
		    renderer.setHdrNits(hdr_paper_white, hdr_peak);
	    });

	toast::settings::declareFloat(
	    "renderer.display.hdr_peak",
	    0.0,
	    {.label = "HDR peak brightness",
			 .category = "Display",
			 .description = "Nits the tonemap rolls highlights off toward when HDR output is on. 0 uses what the display reports, "
			                "1000 when it does not say",
			 .min = 0.0,
			 .max = 4000.0,
			 .step = 50.0}
	)
	    .onChange([&renderer](double v) {
		    hdr_peak = static_cast<float>(v);
		    renderer.setHdrNits(hdr_paper_white, hdr_peak);
	    });
}

/// Dev builds keep Custom and players start on a tier for their GPU
auto defaultPreset(VulkanRenderer& renderer) -> int64_t {
#if defined(TOAST_DEV_TOOLS)
	static_cast<void>(renderer);
	return k_preset_custom;
#else
	const VulkanCore& core = renderer.getCore();
	if (core.isRayTracingSupported()) {
		return k_tiers_per_branch + 2;
	}
	return core.getPhysicalDevice().getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 1 : 0;
#endif
}

void registerQuality(VulkanRenderer& renderer) {
	toast::settings::declareInt(
	    "renderer.quality.preset",
	    defaultPreset(renderer),
	    {
	      .label = "Preset",
	      .category = "Quality",
	      .description = "Reflections, AO, shadows, voxel lighting, volumetrics, GI and render scale together. The RT tiers need "
	                     "hardware ray tracing and run the matching non RT tier without it. Custom uses each setting",
	      .options = {"Low", "Medium", "High", "Epic", "Low RT", "Medium RT", "High RT", "Epic RT", "Custom"}
	}
	)
	    .onChange([](int64_t v) {
		    const auto next = static_cast<uint32_t>(std::clamp<int64_t>(v, 0, k_preset_custom));
		    std::optional<PresetKnobs> seed;
		    {
			    const std::lock_guard lock(g_preset.mutex);
			    if (g_preset.ready && g_preset.preset < k_preset_custom && next == k_preset_custom) {
				    seed = activeKnobs();
				    g_preset.knobs = *seed;
			    }
			    g_preset.preset = next;
		    }
		    if (seed.has_value()) {
			    seedSettingsFrom(*seed);
		    }
		    if (next >= k_tiers_per_branch && next < k_preset_custom && !g_preset.renderer->getCore().isRayTracingSupported()) {
			    TOAST_INFO("Render", "Ray tracing is not in use on this device so the RT preset runs its non RT tier");
		    }
		    applyPreset();
	    });

	toast::settings::declareFloat(
	    "renderer.quality.render_scale",
	    1.0,
	    {.label = "Render scale",
			 .category = "Quality",
			 .description = "Share of the output resolution the world renders at before TAA upsamples it. Preset Custom only",
			 .min = 0.5,
			 .max = 1.0,
			 .step = 0.05}
	)
	    .onChange(presetKnob<float, double>(&PresetKnobs::render_scale, k_to_float));

	const auto pass_toggle = [&renderer](std::string_view key, std::string_view pass, bool default_value, std::string description) {
		toast::settings::declareBool(
		    key, default_value, {.label = std::string {pass}, .category = "Quality", .description = std::move(description)}
		)
		    .onChange([&renderer, pass](bool enabled) { renderer.setPostProcessPassEnabled(pass, enabled); });
	};

	pass_toggle("renderer.quality.bloom", "Bloom", true, "Glow around highlights bright enough to overwhelm a lens.");
	pass_toggle(
	    "renderer.quality.lens_flare",
	    "Lens flare",
	    true,
	    "Ghosts, halo and glare streaks from the brightest pixels. Tuned on the post process volume."
	);
	toast::settings::declareBool(
	    "renderer.quality.ssr",
	    true,
	    {.label = "SSR", .category = "Quality", .description = "Screen-space reflections. Preset Custom only"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::ssr, k_same_bool));
	toast::settings::declareBool(
	    "renderer.quality.ssao",
	    true,
	    {.label = "SSAO",
			 .category = "Quality",
			 .description = "Contact darkening where geometry meets geometry. Preset Custom only"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::ssao, k_same_bool));

	toast::settings::declareBool(
	    "renderer.quality.motion_blur",
	    false,
	    {.label = "Motion blur",
			 .category = "Quality",
			 .description = "Blurs the image along the motion vectors of the frame. Preset Custom only"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::motion_blur, k_same_bool));

	toast::settings::declareFloat(
	    "renderer.quality.motion_blur_strength",
	    0.5,
	    {.label = "Motion blur shutter",
			 .category = "Quality",
			 .description = "Share of the frame the shutter stays open. 0.5 is a 180 degree shutter",
			 .min = 0.0,
			 .max = 1.0,
			 .step = 0.05}
	)
	    .onChange([&renderer](double v) { renderer.setMotionBlurStrength(static_cast<float>(v)); });

	toast::settings::declareFloat(
	    "renderer.quality.motion_blur_max_length",
	    0.04,
	    {.label = "Motion blur max length",
			 .category = "Quality",
			 .description = "Longest blur as a share of the screen height",
			 .min = 0.005,
			 .max = 0.2,
			 .step = 0.005}
	)
	    .onChange([&renderer](double v) { renderer.setMotionBlurMaxLength(static_cast<float>(v)); });

	toast::settings::declareInt(
	    "renderer.quality.motion_blur_samples",
	    12,
	    {.label = "Motion blur samples",
			 .category = "Quality",
			 .description = "Taps along each blur. More is smoother and costs more",
			 .min = 4.0,
			 .max = 32.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setMotionBlurSamples(static_cast<uint32_t>(std::max<int64_t>(v, 2))); });

	toast::settings::declareFloat(
	    "renderer.quality.ssr_reach",
	    0.0,
	    {.label = "SSR reach",
			 .category = "Quality",
			 .description = "Meters an SSR ray travels, with the steps below in place of the post process volume. Preset Custom only",
			 .min = 0.0,
			 .max = 100.0,
			 .step = 1.0}
	)
	    .onChange(presetKnob<float, double>(&PresetKnobs::ssr_reach, k_to_float));

	toast::settings::declareInt(
	    "renderer.quality.ssr_steps",
	    0,
	    {.label = "SSR steps",
			 .category = "Quality",
			 .description = "Samples along each SSR ray. 0 keeps the post process volume values. Preset Custom only",
			 .min = 0.0,
			 .max = 256.0}
	)
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::ssr_steps, k_to_count));
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
	    .onChange(presetKnob<float, double>(&PresetKnobs::shadow_distance, k_to_float));

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
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::spot_budget, k_to_count));

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
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::point_budget, k_to_count));

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
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::light_candidates, [](int64_t v) {
		    return static_cast<uint32_t>(std::max<int64_t>(v, 1));
	    }));

	toast::settings::declareBool(
	    "renderer.voxels.shadow_maps_only",
	    false,
	    {.label = "Shadow maps only",
			 .category = "Voxels",
			 .description = "Every light shadows voxels with its shadow map whatever its Voxel Shadows mode says. Marched and "
			                "Sampled shadows trace scene rays, through ray queries when the GPU traces and else by walking the "
			                "software cell grid. Preset Custom only"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::shadow_maps_only, k_same_bool));

	toast::settings::declareBool(
	    "renderer.voxels.ambient_rays",
	    true,
	    {.label = "Ambient rays",
			 .category = "Voxels",
			 .description = "Two super-sparse rays per voxel pixel occlude ambient light by how far they get, in place of "
			                "SSAO. Off keeps SSAO on voxels"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::ambient_rays, k_same_bool));

	toast::settings::declareBool(
	    "renderer.voxels.ambient_ray_query",
	    true,
	    {.label = "Ray query ambient rays",
			 .category = "Voxels",
			 .description = "Ambient rays take the exact first hit through hardware ray queries when the GPU has them. Off "
			                "samples each volume eight times and can step over thin walls"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelAmbientRayQuery(enabled); });

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
	    "renderer.voxels.traced_specular",
	    true,
	    {.label = "Traced specular",
			 .category = "Voxels",
			 .description = "One GGX ray per reflective voxel pixel at half resolution in place of the environment and "
			                "SSR. Off keeps both"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::traced_specular, k_same_bool));

	toast::settings::declareFloat(
	    "renderer.voxels.specular_roughness",
	    0.6,
	    {.label = "Traced specular roughness",
			 .category = "Voxels",
			 .description = "Rougher voxels keep the environment specular",
			 .min = 0.0,
			 .max = 1.0,
			 .step = 0.05}
	)
	    .onChange(presetKnob<float, double>(&PresetKnobs::specular_roughness, k_to_float));

	toast::settings::declareFloat(
	    "renderer.voxels.specular_reach",
	    32.0,
	    {.label = "Traced specular reach",
			 .category = "Voxels",
			 .description = "Meters a reflection ray travels before it reads the environment. Teardown uses 32",
			 .min = 1.0,
			 .max = 200.0,
			 .step = 1.0}
	)
	    .onChange(presetKnob<float, double>(&PresetKnobs::specular_reach, k_to_float));

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
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::glass_shadows, k_same_bool));

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

	toast::settings::declareBool(
	    "renderer.voxels.trace_bricks",
	    true,
	    {.label = "Brick BLAS",
			 .category = "Voxels",
			 .description = "Ray queries find the occupied bricks a ray crosses instead of whole volumes. Off marches every "
			                "volume a ray crosses from end to end"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVoxelTraceBricks(enabled); });

	toast::settings::declareInt(
	    "renderer.voxels.blas_budget",
	    262144,
	    {.label = "BLAS bricks per frame",
			 .category = "Voxels",
			 .description = "Bricks of brick BLAS builds a frame may record. A volume waiting for its build traces as a whole",
			 .min = 4096.0,
			 .max = 4194304.0}
	)
	    .onChange([&renderer](int64_t v) {
		    renderer.setVoxelBlasBudget(static_cast<uint32_t>(std::clamp<int64_t>(v, 4096, 4194304)));
	    });

	toast::settings::declareFloat(
	    "renderer.voxels.trace_cell_size",
	    4.0,
	    {.label = "Trace grid cell",
			 .category = "Voxels",
			 .description = "Meters per cell of the camera centred grid marched voxel rays walk without ray queries. The grid "
			                "is 32 cells across",
			 .min = 0.5,
			 .max = 32.0,
			 .step = 0.5}
	)
	    .onChange([&renderer](double v) { renderer.setVoxelTraceCellSize(static_cast<float>(v)); });

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

void registerVolumetrics(VulkanRenderer& renderer) {
	toast::settings::declareBool(
	    "renderer.volumetrics.enabled",
	    true,
	    {.label = "Volumetrics",
			 .category = "Volumetrics",
			 .description = "Marched height fog where a post process volume sets a fog density. Off skips the pass"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVolumetrics(enabled); });

	toast::settings::declareInt(
	    "renderer.volumetrics.quality",
	    2,
	    {
	      .label = "Volumetric quality",
	      .category = "Volumetrics",
	      .description = "Steps, local lights, local light shadows and resolution. Custom uses the settings below",
	      .options = {"Low", "Medium", "High", "Epic", "Custom"}
	}
	)
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::volumetric_quality, [](int64_t v) {
		    return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, VulkanRenderer::k_volumetric_quality_custom));
	    }));

	toast::settings::declareInt(
	    "renderer.volumetrics.steps",
	    24,
	    {.label = "Volumetric steps",
			 .category = "Volumetrics",
			 .description = "Samples along each view ray. Custom quality only",
			 .min = 4.0,
			 .max = 96.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVolumetricSteps(static_cast<uint32_t>(std::max<int64_t>(v, 1))); });

	toast::settings::declareInt(
	    "renderer.volumetrics.lights",
	    8,
	    {.label = "Volumetric lights",
			 .category = "Volumetrics",
			 .description = "Local lights of a froxel each step scatters. Custom quality only",
			 .min = 0.0,
			 .max = 32.0}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVolumetricLights(static_cast<uint32_t>(std::max<int64_t>(v, 0))); });

	toast::settings::declareBool(
	    "renderer.volumetrics.light_shadows",
	    true,
	    {.label = "Volumetric light shadows",
			 .category = "Volumetrics",
			 .description = "Local lights cast shadows into the fog. Custom quality only"}
	)
	    .onChange([&renderer](bool enabled) { renderer.setVolumetricLightShadows(enabled); });

	toast::settings::declareInt(
	    "renderer.volumetrics.resolution",
	    0,
	    {
	      .label = "Volumetric resolution",
	      .category = "Volumetrics",
	      .description = "Custom quality only",
	      .options = {"Half", "Quarter"}
	}
	)
	    .onChange([&renderer](int64_t v) { renderer.setVolumetricQuarterResolution(v != 0); });

	toast::settings::declareFloat(
	    "renderer.volumetrics.distance",
	    100.0,
	    {.label = "Volumetric distance",
			 .category = "Volumetrics",
			 .description = "Meters fog volumes stay visible. Height fog keeps its own Fog Distance",
			 .min = 10.0,
			 .max = 1000.0,
			 .step = 5.0}
	)
	    .onChange([&renderer](double v) { renderer.setVolumetricDistance(static_cast<float>(v)); });
}

#if defined(TOAST_DEV_TOOLS)
void registerDebug(VulkanRenderer& renderer) {
	toast::settings::declareBool(
	    k_debug_overlay_in_game,
	    false,
	    {.label = "Debug overlay in game",
			 .category = "Debug",
			 .description = "Draws the ImGui debug windows while a game camera is live and frees the cursor to use them. "
			                "Off hides them in game and brings them back as they were in the editor. F3 in game flips it."}
	)
	    .onChange([&renderer](bool visible) { renderer.setDebugOverlayInGame(visible); });
}
#endif

}

void registerRendererStartupSettings() {
	registerShadows();

	toast::settings::declareBool(
	    k_hdr_output,
	    true,
	    {.label = "HDR output",
			 .category = "Display",
			 .description = "Presents HDR when the display has it turned on in the operating system and SDR otherwise. Off keeps "
			                "SDR on every display",
			 .requires_restart = true}
	);

	toast::settings::declareInt(
	    "renderer.quality.ray_tracing",
	    0,
	    {
	      .label = "Hardware ray tracing",
	      .category = "Quality",
	      .description = "Auto traces only on GPUs with RT cores since drivers emulate ray query slowly elsewhere. Off keeps the "
	                     "raster and software paths on every GPU, which is what the non RT presets are tuned for. On forces ray "
	                     "query wherever the driver offers it",
	      .options = {"Auto", "On", "Off"},
	      .requires_restart = true
	}
	)
	    .onChange([](int64_t v) {
		    constexpr std::array<RayTracingMode, 3> k_modes {RayTracingMode::automatic, RayTracingMode::on, RayTracingMode::off};
		    ShaderCompiler::setRayTracingMode(k_modes[static_cast<size_t>(std::clamp<int64_t>(v, 0, 2))]);
	    });

	toast::settings::declareBool(
	    "renderer.voxels.trace_ray_query",
	    true,
	    {.label = "Ray query voxel rays",
			 .category = "Voxels",
			 .description = "Marched voxel rays find the volumes they cross through hardware ray queries when the GPU has them. "
			                "Off walks the software cell grid on every GPU",
			 .requires_restart = true}
	)
	    .onChange([](bool enabled) { ShaderCompiler::setTraceRayQuery(enabled); });
}

void registerGi(VulkanRenderer& renderer) {
	GiSettings& gi = renderer.giSettings();
	toast::settings::declareBool(
	    "renderer.gi.enabled",
	    true,
	    {.label = "Radiance cascades",
			 .category = "Global illumination",
			 .description = "World space radiance cascade probes. Preset Custom only"}
	)
	    .onChange(presetKnob<bool, bool>(&PresetKnobs::gi_enabled, k_same_bool));

	toast::settings::declareInt(
	    "renderer.gi.quality",
	    2,
	    {
	      .label = "Quality",
	      .category = "Global illumination",
	      .description = "Rays per frame, cascades, probe spacing, multibounce and update rate. Custom uses the knobs below",
	      .options = {"Low", "Medium", "High", "Epic", "Custom"}
	}
	)
	    .onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::gi_quality, [](int64_t v) {
		    return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, GiSettings::k_quality_custom));
	    }));

	toast::settings::declareInt(
	    "renderer.gi.update_interval",
	    1,
	    {.label = "Update interval",
			 .category = "Global illumination",
			 .description = "Rendered frames per GI update. Custom quality only",
			 .min = 1.0,
			 .max = 4.0}
	)
	    .onChange([&gi](int64_t v) {
		    gi.update_interval.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 4)), std::memory_order_relaxed);
	    });

	toast::settings::declareInt(
	    "renderer.gi.secondary_points",
	    65536,
	    {.label = "Multibounce points",
			 .category = "Global illumination",
			 .description = "Off screen hits kept for the secondary cache per frame. Custom quality only",
			 .min = 1024.0,
			 .max = 65536.0}
	)
	    .onChange([&gi](int64_t v) {
		    gi.secondary_points.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1024, 65536)), std::memory_order_relaxed);
	    });

	toast::settings::declareBool(
	    "renderer.gi.multibounce",
	    true,
	    {.label = "Multibounce",
			 .category = "Global illumination",
			 .description = "GI rays that hit something the camera cannot see seed a coarser secondary probe cache and read it "
			                "back, so light keeps bouncing off screen. Costs about half again the rays. Custom quality only"}
	)
	    .onChange([&gi](bool enabled) { gi.multibounce.store(enabled, std::memory_order_relaxed); });

	toast::settings::declareFloat(
	    "renderer.gi.probe_spacing",
	    0.1,
	    {.label = "Probe spacing",
			 .category = "Global illumination",
			 .description =
			     "Meters between cascade 0 probes near the camera. Every cascade and every LOD doubles it. Custom quality only",
			 .min = 0.025,
			 .max = 1.0,
			 .step = 0.025}
	)
	    .onChange([&gi](double v) { gi.probe_spacing.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareFloat(
	    "renderer.gi.lod_distance",
	    4.0,
	    {.label = "LOD distance",
			 .category = "Global illumination",
			 .description = "Meters from the camera where probes start doubling their spacing. Custom quality only",
			 .min = 0.5,
			 .max = 64.0,
			 .step = 0.5}
	)
	    .onChange([&gi](double v) { gi.lod_distance.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareInt(
	    "renderer.gi.cascades",
	    5,
	    {.label = "Cascades",
			 .category = "Global illumination",
			 .description = "Each cascade reaches four times farther than the one below. Custom quality only",
			 .min = 1.0,
			 .max = 6.0}
	)
	    .onChange([&gi](int64_t v) {
		    gi.cascades.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 6)), std::memory_order_relaxed);
	    });

	toast::settings::declareInt(
	    "renderer.gi.ray_resolution",
	    1,
	    {
	      .label = "Ray resolution",
	      .category = "Global illumination",
	      .description = "Pixels that place probes and trace rays each frame. Custom quality only",
	      .options = {"Full", "Half", "Quarter"}
	}
	)
	    .onChange([&gi](int64_t v) {
		    constexpr std::array<uint32_t, 3> k_steps {1, 2, 4};
		    gi.ray_step.store(k_steps[static_cast<size_t>(std::clamp<int64_t>(v, 0, 2))], std::memory_order_relaxed);
	    });

	toast::settings::declareInt(
	    "renderer.gi.probe_lifetime",
	    60,
	    {.label = "Probe lifetime",
			 .category = "Global illumination",
			 .description = "Frames a probe stays after no pixel needs it",
			 .min = 1.0,
			 .max = 600.0}
	)
	    .onChange([&gi](int64_t v) {
		    gi.probe_lifetime.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 600)), std::memory_order_relaxed);
	    });

	toast::settings::declareFloat(
	    "renderer.gi.history_seconds",
	    0.3,
	    {.label = "History length",
			 .category = "Global illumination",
			 .description = "Seconds of light every probe direction averages, the same at any frame rate. Longer is smoother "
			                "in dark places lit only through gaps but follows moving lights more slowly, 0 keeps no history. "
			                "Voxel changes and moving bodies reset the probes around them either way",
			 .min = 0.0,
			 .max = 5.0,
			 .step = 0.05}
	)
	    .onChange([&gi](double v) { gi.history_seconds.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareBool(
	    "renderer.gi.history_per_frame",
	    false,
	    {.label = "Per frame history",
			 .category = "Global illumination",
			 .description = "Blend a fixed share of every frame into the history instead of a length in seconds. Less ghosting "
			                "at high frame rates, more flicker where light only reaches through thin gaps"}
	)
	    .onChange([&gi](bool enabled) { gi.history_per_frame.store(enabled, std::memory_order_relaxed); });

	toast::settings::declareFloat(
	    "renderer.gi.history",
	    0.03,
	    {.label = "History blend",
			 .category = "Global illumination",
			 .description = "Share of each new frame in the light of every probe direction while Per frame history is on. "
			                "Lower is smoother but follows moving lights more slowly, 1 keeps no history",
			 .min = 0.005,
			 .max = 1.0,
			 .step = 0.005}
	)
	    .onChange([&gi](double v) { gi.history_blend.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareFloat(
	    "renderer.gi.motion_threshold",
	    0.01,
	    {.label = "Motion threshold",
			 .category = "Global illumination",
			 .description = "Meters a voxel body has to move in one frame before the probes around it drop their history. "
			                "Raise it when resting physics bodies jitter and the perf window keeps reporting moved bodies",
			 .min = 0.0,
			 .max = 0.2,
			 .step = 0.001}
	)
	    .onChange([&gi](double v) { gi.motion_threshold.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareFloat(
	    "renderer.gi.max_ray_radiance",
	    16.0,
	    {.label = "Ray brightness clamp",
			 .category = "Global illumination",
			 .description = "Brightest a single GI ray hit may count as, keeping its colour. Rays rarely catch a small very bright "
			                "emitter like a lamp shade, so each catch jumps the light around it. Lower is steadier, higher keeps "
			                "more bounce from bright emitters",
			 .min = 0.5,
			 .max = 1000.0,
			 .step = 0.5}
	)
	    .onChange([&gi](double v) { gi.max_ray_radiance.store(static_cast<float>(v), std::memory_order_relaxed); });

	toast::settings::declareInt(
	    "renderer.gi.debug_view",
	    0,
	    {
	      .label = "Probe view",
	      .category = "Global illumination",
	      .description = "Draws the probe cells of one cascade over the scene. Red has no probe and dim was carried from an "
	                     "earlier frame",
	      .options = {"Off", "Cascade 0", "Cascade 1", "Cascade 2", "Cascade 3", "Cascade 4", "Cascade 5"}
	}
	)
	    .onChange([&gi](int64_t v) {
		    gi.debug_view.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 6)), std::memory_order_relaxed);
	    });

	toast::settings::declareInt(
	    "renderer.gi.debug_mode",
	    0,
	    {
	      .label = "Probe view shows",
	      .category = "Global illumination",
	      .description = "Probe cells, the accumulated radiance of the cascade interval, the share of directions facing the "
	                     "surface that got a ray this frame, the merged irradiance lighting will read, which ignores the "
	                     "cascade picked, or how many rays of history the directions facing the surface hold, red none, "
	                     "yellow about 8, green 64 and up, or how much the irradiance at each pixel changes per frame, black "
	                     "steady, blue 1%, yellow 5%, red 15% and up. Purple probes have no storage",
	      .options = {"Probe cells", "Interval radiance", "Coverage", "Irradiance", "History depth", "Irradiance change"}
	}
	)
	    .onChange([&gi](int64_t v) {
		    gi.debug_mode.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 5)), std::memory_order_relaxed);
	    });
}

auto presetDrivenValue(std::string_view key) -> std::optional<toast::settings::Value> {
	const std::lock_guard lock(g_preset.mutex);
	if (g_preset.preset >= k_preset_custom) {
		return std::nullopt;
	}
	for (const DrivenSetting& driven : k_driven) {
		if (driven.key == key) {
			return driven.get(activeKnobs());
		}
	}
	return std::nullopt;
}

void registerRendererSettings(VulkanRenderer& renderer) {
	g_preset.renderer = &renderer;
	registerDisplay(renderer);
	registerQuality(renderer);
	registerVoxels(renderer);
	registerGi(renderer);
	registerEnvironment(renderer);
	registerVolumetrics(renderer);
#if defined(TOAST_DEV_TOOLS)
	registerDebug(renderer);
#endif
	g_preset.ready = true;
	applyPreset();
}

}
