#include "toast/renderer/settings/renderer_settings.hpp"

#include "toast/renderer/lighting/shadow_constants.hpp"
#include "toast/renderer/lighting/shadow_slots.hpp"
#include "toast/renderer/passes/lighting/environment_pass.hpp"
#include "toast/renderer/settings/renderer_params.hpp"
#include "toast/renderer/shaders/shader_compiler.hpp"
#include "toast/renderer/vulkan_renderer.hpp"

#include <algorithm>
#include <toast/assets/asset_manager.hpp>
#include <toast/params/registry.hpp>
#include <toast/world/workspace_events.hpp>

namespace renderer {
namespace {

constexpr uint32_t k_tiers_per_branch = 4;
constexpr uint32_t k_preset_custom = 2 * k_tiers_per_branch;

struct PresetKnobs {
	bool ssr = true;
	bool ssao = true;
	bool traced_specular = true;
	float specular_roughness = 0.6f;
	float specular_reach = 32.0f;
	/// Off screen traced specular hits take a local light and its shadow ray
	bool specular_hit_lights = true;
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
	uint32_t glass_lit_surfaces = 4;
	/// Meters glass reflections reach, below the SSR and traced specular reach since only the front layer reflects
	float glass_reflection_reach = 32.0f;
};

/// Low Medium High Epic without ray tracing, then Low RT Medium RT High RT Epic RT. Radiance cascades run in every tier
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
		 .gi_enabled = true,
		 .render_scale = 0.65f,
		 .shadow_distance = 60.0f,
		 .spot_budget = 4,
		 .point_budget = 2,
		 .shadow_maps_only = true,
		 .glass_lit_surfaces = 1,
		 .glass_reflection_reach = 8.0f},
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
	   .gi_enabled = true,
	   .render_scale = 0.75f,
	   .shadow_distance = 100.0f,
	   .spot_budget = 6,
	   .point_budget = 3,
	   .shadow_maps_only = true,
	   .ssr_reach = 50.0f,
	   .ssr_steps = 48,
	   .glass_lit_surfaces = 2,
	   .glass_reflection_reach = 12.0f},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.5f,
	   .specular_reach = 32.0f,
	   .light_candidates = 6,
	   .ambient_rays = false,
	   .glass_shadows = true,
	   .volumetric_quality = 1,
	   .gi_quality = 1,
	   .gi_enabled = true,
	   .render_scale = 0.85f,
	   .shadow_distance = 150.0f,
	   .spot_budget = 10,
	   .point_budget = 5,
	   .shadow_maps_only = true,
	   .ssr_reach = 100.0f,
	   .ssr_steps = 64,
	   .motion_blur = true,
	   .glass_lit_surfaces = 3,
	   .glass_reflection_reach = 16.0f},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = false,
	   .specular_roughness = 0.5f,
	   .specular_reach = 32.0f,
	   .light_candidates = 8,
	   .ambient_rays = false,
	   .glass_shadows = true,
	   .volumetric_quality = 1,
	   .gi_quality = 2,
	   .gi_enabled = true,
	   .render_scale = 1.0f,
	   .shadow_distance = 250.0f,
	   .spot_budget = 16,
	   .point_budget = 8,
	   .shadow_maps_only = true,
	   .ssr_reach = 200.0f,
	   .ssr_steps = 96,
	   .motion_blur = true,
	   .glass_lit_surfaces = 4,
	   .glass_reflection_reach = 24.0f},
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
		 .gi_enabled = true,
		 .render_scale = 0.75f,
		 .shadow_distance = 60.0f,
		 .spot_budget = 4,
		 .point_budget = 2,
		 .shadow_maps_only = true,
		 .glass_lit_surfaces = 1,
		 .glass_reflection_reach = 8.0f},
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
	   .gi_enabled = true,
	   .render_scale = 0.85f,
	   .shadow_distance = 100.0f,
	   .spot_budget = 8,
	   .point_budget = 4,
	   .ssr_reach = 50.0f,
	   .ssr_steps = 48,
	   .glass_lit_surfaces = 2,
	   .glass_reflection_reach = 16.0f},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = true,
	   .specular_roughness = 0.45f,
	   .specular_reach = 24.0f,
	   .specular_hit_lights = false,
	   .light_candidates = 8,
	   .ambient_rays = true,
	   .glass_shadows = true,
	   .volumetric_quality = 2,
	   .gi_quality = 2,
	   .gi_enabled = true,
	   .shadow_distance = 150.0f,
	   .spot_budget = 16,
	   .point_budget = 8,
	   .ssr_reach = 100.0f,
	   .ssr_steps = 64,
	   .motion_blur = true,
	   .glass_lit_surfaces = 3,
	   .glass_reflection_reach = 24.0f},
   {.ssr = true,
	   .ssao = true,
	   .traced_specular = true,
	   .specular_roughness = 0.6f,
	   .specular_reach = 32.0f,
	   .light_candidates = 16,
	   .ambient_rays = true,
	   .glass_shadows = true,
	   .volumetric_quality = 3,
	   .gi_quality = 3,
	   .gi_enabled = true,
	   .shadow_distance = 250.0f,
	   .spot_budget = shadows::k_max_spot_shadows,
	   .point_budget = shadows::k_max_point_shadows,
	   .ssr_reach = 200.0f,
	   .ssr_steps = 96,
	   .motion_blur = true,
	   .glass_lit_surfaces = 4,
	   .glass_reflection_reach = 32.0f},
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
	renderer.setVoxelSpecularHitLights(knobs.specular_hit_lights);
	renderer.setVoxelLightCandidates(knobs.light_candidates);
	renderer.setVoxelAmbientRays(knobs.ambient_rays);
	renderer.setVoxelGlassShadows(knobs.glass_shadows);
	renderer.setVoxelGlassLitSurfaces(knobs.glass_lit_surfaces);
	renderer.setVoxelGlassReflectionReach(knobs.glass_reflection_reach);
	renderer.setVolumetricQuality(knobs.volumetric_quality);
	renderer.giSettings().quality.store(knobs.gi_quality, std::memory_order_relaxed);
	shadows::setShadowDistance(knobs.shadow_distance);
	shadows::setSpotShadowBudget(knobs.spot_budget);
	shadows::setPointShadowBudget(knobs.point_budget);
}

using toast::params::Value;

struct DrivenSetting {
	std::string_view key;
	Value (*get)(const PresetKnobs&);
};

const std::array<DrivenSetting, 22> k_driven {
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
   {"renderer.voxels.specular_hit_lights", [](const PresetKnobs& k) { return Value {k.specular_hit_lights}; }},
   {"renderer.voxels.light_candidates", [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.light_candidates)}; }},
   {"renderer.voxels.ambient_rays", [](const PresetKnobs& k) { return Value {k.ambient_rays}; }},
   {"renderer.voxels.glass_shadows", [](const PresetKnobs& k) { return Value {k.glass_shadows}; }},
   {"renderer.voxels.glass_lit_surfaces",
	   [](const PresetKnobs& k) { return Value {static_cast<int64_t>(k.glass_lit_surfaces)}; }},
   {"renderer.voxels.glass_reflection_reach",
	   [](const PresetKnobs& k) { return Value {static_cast<double>(k.glass_reflection_reach)}; }},
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
	auto& registry = toast::params::Registry::get();
	for (const DrivenSetting& driven : k_driven) {
		registry.request(std::string {driven.key}, driven.get(tier), registry.authoringLayer());
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
	auto& p = rendererParams();
	p.display_clamp_to_simulation.onChange([&renderer](bool clamp) { renderer.setClampToSimulation(clamp); });

	p.display_low_latency.onChange([&renderer](bool enabled) { renderer.setLowLatency(enabled); });

	p.display_frame_rate_limit.onChange([&renderer](double v) { renderer.setFrameRateLimit(v); });

	static float hdr_paper_white = 0.0f;
	static float hdr_peak = 0.0f;
	p.display_hdr_paper_white.onChange([&renderer](double v) {
		hdr_paper_white = static_cast<float>(v);
		renderer.setHdrNits(hdr_paper_white, hdr_peak);
	});

	p.display_hdr_peak.onChange([&renderer](double v) {
		hdr_peak = static_cast<float>(v);
		renderer.setHdrNits(hdr_paper_white, hdr_peak);
	});
}

/// Dev builds keep Custom and players start on a tier for their GPU
[[maybe_unused]]
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
	auto& p = rendererParams();
	p.quality_preset.onChange([](int64_t v) {
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

#if !defined(TOAST_DEV_TOOLS)
	if (!p.quality_preset.layerValue(toast::params::Layer::project) && !p.quality_preset.layerValue(toast::params::Layer::saved)) {
		p.quality_preset.setLayer(toast::params::Layer::project, Value {toast::params::Scalar {defaultPreset(renderer)}});
	}
#endif

	p.quality_render_scale.onChange(presetKnob<float, double>(&PresetKnobs::render_scale, k_to_float));

	p.quality_bloom.onChange([&renderer](bool enabled) { renderer.setPostProcessPassEnabled("Bloom", enabled); });
	p.quality_lens_flare.onChange([&renderer](bool enabled) { renderer.setPostProcessPassEnabled("Lens flare", enabled); });
	p.quality_glass_glow_scale.onChange([&renderer](double v) { renderer.setGlassGlowScale(static_cast<float>(v)); });
	p.quality_ssr.onChange(presetKnob<bool, bool>(&PresetKnobs::ssr, k_same_bool));
	p.quality_ssao.onChange(presetKnob<bool, bool>(&PresetKnobs::ssao, k_same_bool));

	p.quality_motion_blur.onChange(presetKnob<bool, bool>(&PresetKnobs::motion_blur, k_same_bool));

	p.quality_motion_blur_strength.onChange([&renderer](double v) { renderer.setMotionBlurStrength(static_cast<float>(v)); });

	p.quality_motion_blur_max_length.onChange([&renderer](double v) { renderer.setMotionBlurMaxLength(static_cast<float>(v)); });

	p.quality_motion_blur_samples.onChange([&renderer](int64_t v) {
		renderer.setMotionBlurSamples(static_cast<uint32_t>(std::max<int64_t>(v, 2)));
	});

	p.quality_ssr_reach.onChange(presetKnob<float, double>(&PresetKnobs::ssr_reach, k_to_float));

	p.quality_ssr_steps.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::ssr_steps, k_to_count));
	p.quality_fxaa.onChange([&renderer](bool enabled) { renderer.setPostProcessPassEnabled("FXAA", enabled); });

	p.quality_taa.onChange([&renderer](bool enabled) { renderer.setTaaEnabled(enabled); });

	p.quality_taa_history.onChange([&renderer](double v) { renderer.setTaaHistoryWeight(static_cast<float>(v)); });

	p.quality_traced_shadows.onChange([&renderer](bool enabled) { renderer.setTracedShadowsEnabled(enabled); });
}

void registerShadows() {
	auto& p = rendererParams();
	const auto resolutions = std::vector<std::string> {"256", "512", "1024", "2048", "4096"};

	p.shadows_cascade_resolution.onChange([resolutions](int64_t index) {
		if (index >= 0 && std::cmp_less(index, resolutions.size())) {
			shadows::setCascadeResolution(static_cast<uint32_t>(std::stoul(resolutions[index])));
		}
	});

	p.shadows_punctual_resolution.onChange([resolutions](int64_t index) {
		if (index >= 0 && std::cmp_less(index, resolutions.size())) {
			shadows::setPunctualResolution(static_cast<uint32_t>(std::stoul(resolutions[index])));
		}
	});

	p.shadows_distance.onChange(presetKnob<float, double>(&PresetKnobs::shadow_distance, k_to_float));

	p.shadows_slot_retention.onChange([](double v) { shadow_slots::setRetention(static_cast<float>(v)); });

	p.shadows_slot_grace.onChange([](int64_t frames) {
		shadow_slots::setGraceFrames(static_cast<uint32_t>(std::max<int64_t>(frames, 0)));
	});

	p.shadows_spot_slots.onChange([](int64_t slots) {
		shadows::setSpotShadowSlots(static_cast<uint32_t>(std::max<int64_t>(slots, 0)));
	});

	p.shadows_point_slots.onChange([](int64_t slots) {
		shadows::setPointShadowSlots(static_cast<uint32_t>(std::max<int64_t>(slots, 0)));
	});

	p.shadows_spot_budget.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::spot_budget, k_to_count));

	p.shadows_point_budget.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::point_budget, k_to_count));

	p.shadows_full_resolution_ranges.onChange([](double ranges) {
		shadows::setPunctualFullResolutionRanges(static_cast<float>(ranges));
	});

	p.shadows_cascade_split_lambda.onChange([](double lambda) { shadows::setCascadeSplitLambda(static_cast<float>(lambda)); });

	p.shadows_cascade_padding.onChange([](double padding) { shadows::setCascadePadding(static_cast<float>(padding)); });

	p.shadows_cascade_receiver_depth.onChange([](double radii) {
		shadows::setCascadeReceiverDepthRadii(static_cast<float>(radii));
	});

	p.shadows_static_cache.onChange([](bool enabled) { shadows::setStaticCasterCache(enabled); });

	p.shadows_caster_tolerance.onChange([](double meters) { shadows::setCasterTolerance(static_cast<float>(meters)); });

	p.shadows_voxel_mesh_shaders.onChange([](bool enabled) { shadows::setVoxelMeshShadows(enabled); });
}

void registerVoxels(VulkanRenderer& renderer) {
	auto& p = rendererParams();
	p.voxels_light_candidates.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::light_candidates, [](int64_t v) {
		return static_cast<uint32_t>(std::max<int64_t>(v, 1));
	}));

	p.voxels_shadow_maps_only.onChange(presetKnob<bool, bool>(&PresetKnobs::shadow_maps_only, k_same_bool));

	p.voxels_ambient_rays.onChange(presetKnob<bool, bool>(&PresetKnobs::ambient_rays, k_same_bool));

	p.voxels_ambient_ray_query.onChange([&renderer](bool enabled) { renderer.setVoxelAmbientRayQuery(enabled); });

	p.voxels_ambient_reach.onChange([&renderer](double v) { renderer.setVoxelAmbientReach(static_cast<float>(v)); });

	p.voxels_traced_specular.onChange(presetKnob<bool, bool>(&PresetKnobs::traced_specular, k_same_bool));

	p.voxels_specular_roughness.onChange(presetKnob<float, double>(&PresetKnobs::specular_roughness, k_to_float));

	p.voxels_specular_reach.onChange(presetKnob<float, double>(&PresetKnobs::specular_reach, k_to_float));

	p.voxels_specular_hit_lights.onChange(presetKnob<bool, bool>(&PresetKnobs::specular_hit_lights, k_same_bool));

	p.voxels_glass_lit_surfaces.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::glass_lit_surfaces, [](int64_t v) {
		return static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 4));
	}));

	p.voxels_glass_reflection_reach.onChange(presetKnob<float, double>(&PresetKnobs::glass_reflection_reach, k_to_float));

	p.voxels_glass_coarse_distance.onChange([&renderer](double v) { renderer.setVoxelGlassCoarseDistance(static_cast<float>(v)); });

	p.voxels_glass_shadows.onChange(presetKnob<bool, bool>(&PresetKnobs::glass_shadows, k_same_bool));

	p.voxels_glass_shadow_reach.onChange([&renderer](int64_t v) {
		renderer.setVoxelGlassShadowReach(static_cast<uint32_t>(std::max<int64_t>(v, 1)));
	});

	p.voxels_sun_reach.onChange([&renderer](int64_t v) {
		renderer.setVoxelSunReach(static_cast<uint32_t>(std::max<int64_t>(v, 1)));
	});

	p.voxels_detail_texture.onChange([&renderer](const toast::params::AssetRef& asset) {
		const std::string& v = asset.uri;
		if (v.empty()) {
			renderer.setVoxelDetailTexture({});
		} else if (v.contains("://")) {
			renderer.setVoxelDetailTexture(assets::load<assets::DetailTexture>(v));
		} else {
			renderer.setVoxelDetailTexture(assets::load<assets::DetailTexture>(toast::UID(toast::UID::fromString(v))));
		}
	});

	p.voxels_trace_bricks.onChange([&renderer](bool enabled) { renderer.setVoxelTraceBricks(enabled); });

	p.voxels_blas_budget.onChange([&renderer](int64_t v) {
		renderer.setVoxelBlasBudget(static_cast<uint32_t>(std::clamp<int64_t>(v, 4096, 4194304)));
	});

	p.voxels_trace_cell_size.onChange([&renderer](double v) { renderer.setVoxelTraceCellSize(static_cast<float>(v)); });

	p.voxels_shadow_instances.onChange([&renderer](int64_t v) {
		renderer.setVoxelShadowInstances(static_cast<uint32_t>(std::max<int64_t>(v, 1)));
	});

	p.voxels_cell_boxes.onChange([&renderer](bool enabled) { renderer.setVoxelCellBoxes(enabled); });

	p.voxels_occlusion.onChange([&renderer](bool enabled) { renderer.setVoxelOcclusion(enabled); });

	p.voxels_mesh_shaders.onChange([&renderer](bool enabled) { renderer.setVoxelMeshShaders(enabled); });

	p.voxels_raster_pixels.onChange([&renderer](double pixels) { renderer.setVoxelRasterPixels(static_cast<float>(pixels)); });

	p.voxels_occlusion_min_hidden.onChange([&renderer](double share) {
		renderer.setVoxelOcclusionMinHidden(static_cast<float>(share));
	});

	p.voxels_occlusion_idle_frames.onChange([&renderer](int64_t frames) {
		renderer.setVoxelOcclusionIdleFrames(static_cast<uint32_t>(std::max<int64_t>(frames, 0)));
	});
}

void registerEnvironment(VulkanRenderer& renderer) {
	auto& p = rendererParams();
	p.environment_map.onChange([&renderer](const toast::params::AssetRef& asset) {
		const std::string& v = asset.uri;
		auto* pass = renderer.getEnvironmentPassMutable();
		if (pass == nullptr) {
			return;
		}
		if (v.empty() || v.contains("://")) {
			pass->setEnvironmentMap(v);
		} else {
			pass->setEnvironmentMap(assets::AssetManager::getURI(toast::UID(toast::UID::fromString(v))));
		}
	});

	p.environment_sky_intensity.onChange([&renderer](double v) {
		if (auto* pass = renderer.getEnvironmentPassMutable()) {
			pass->setSkyIntensity(static_cast<float>(v));
		}
	});
}

void registerVolumetrics(VulkanRenderer& renderer) {
	auto& p = rendererParams();
	p.volumetrics_enabled.onChange([&renderer](bool enabled) { renderer.setVolumetrics(enabled); });

	p.volumetrics_quality.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::volumetric_quality, [](int64_t v) {
		return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, VulkanRenderer::k_volumetric_quality_custom));
	}));

	p.volumetrics_steps.onChange([&renderer](int64_t v) {
		renderer.setVolumetricSteps(static_cast<uint32_t>(std::max<int64_t>(v, 1)));
	});

	p.volumetrics_lights.onChange([&renderer](int64_t v) {
		renderer.setVolumetricLights(static_cast<uint32_t>(std::max<int64_t>(v, 0)));
	});

	p.volumetrics_light_shadows.onChange([&renderer](bool enabled) { renderer.setVolumetricLightShadows(enabled); });

	p.volumetrics_resolution.onChange([&renderer](int64_t v) { renderer.setVolumetricQuarterResolution(v != 0); });

	p.volumetrics_distance.onChange([&renderer](double v) { renderer.setVolumetricDistance(static_cast<float>(v)); });
}

#if defined(TOAST_DEV_TOOLS)
void registerDebug(VulkanRenderer& renderer) {
	auto& p = rendererParams();
	p.display_debug_overlay.onChange([&renderer](bool visible) { renderer.setDebugOverlayInGame(visible); });
	p.view_mode.onChange([](const int64_t& mode) {
		event::send<event::SetRenderMode>(static_cast<uint32_t>(std::max<int64_t>(mode, 0)));
	});

	p.voxels_glass_cost_view.onChange([&renderer](bool enabled) { renderer.setGlassCostView(enabled); });
}
#endif

}

auto rendererParams() -> RendererParams& {
	static RendererParams params;
	return params;
}

void testSetPreset(uint32_t preset) {
	const std::lock_guard lock(g_preset.mutex);
	g_preset.preset = preset;
}

void registerRendererStartupSettings() {
	auto& p = rendererParams();
	registerShadows();

	p.quality_ray_tracing.onChange([](int64_t v) {
		constexpr std::array<RayTracingMode, 3> k_modes {RayTracingMode::automatic, RayTracingMode::on, RayTracingMode::off};
		ShaderCompiler::setRayTracingMode(k_modes[static_cast<size_t>(std::clamp<int64_t>(v, 0, 2))]);
	});

	p.voxels_trace_ray_query.onChange([](bool enabled) { ShaderCompiler::setTraceRayQuery(enabled); });
}

void registerGi(VulkanRenderer& renderer) {
	auto& p = rendererParams();
	GiSettings& gi = renderer.giSettings();
	p.gi_enabled.onChange(presetKnob<bool, bool>(&PresetKnobs::gi_enabled, k_same_bool));

	p.gi_quality.onChange(presetKnob<uint32_t, int64_t>(&PresetKnobs::gi_quality, [](int64_t v) {
		return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, GiSettings::k_quality_custom));
	}));

	p.gi_update_interval.onChange([&gi](int64_t v) {
		gi.update_interval.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 4)), std::memory_order_relaxed);
	});

	p.gi_secondary_points.onChange([&gi](int64_t v) {
		gi.secondary_points.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1024, 65536)), std::memory_order_relaxed);
	});

	p.gi_multibounce.onChange([&gi](bool enabled) { gi.multibounce.store(enabled, std::memory_order_relaxed); });

	p.gi_probe_spacing.onChange([&gi](double v) { gi.probe_spacing.store(static_cast<float>(v), std::memory_order_relaxed); });

	p.gi_lod_distance.onChange([&gi](double v) { gi.lod_distance.store(static_cast<float>(v), std::memory_order_relaxed); });

	p.gi_cascades.onChange([&gi](int64_t v) {
		gi.cascades.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 6)), std::memory_order_relaxed);
	});

	p.gi_ray_resolution.onChange([&gi](int64_t v) {
		constexpr std::array<uint32_t, 3> k_steps {1, 2, 4};
		gi.ray_step.store(k_steps[static_cast<size_t>(std::clamp<int64_t>(v, 0, 2))], std::memory_order_relaxed);
	});

	p.gi_probe_lifetime.onChange([&gi](int64_t v) {
		gi.probe_lifetime.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 600)), std::memory_order_relaxed);
	});

	p.gi_history_seconds.onChange([&gi](double v) { gi.history_seconds.store(static_cast<float>(v), std::memory_order_relaxed); });

	p.gi_history_per_frame.onChange([&gi](bool enabled) { gi.history_per_frame.store(enabled, std::memory_order_relaxed); });

	p.gi_history.onChange([&gi](double v) { gi.history_blend.store(static_cast<float>(v), std::memory_order_relaxed); });

	p.gi_motion_threshold.onChange([&gi](double v) {
		gi.motion_threshold.store(static_cast<float>(v), std::memory_order_relaxed);
	});

	p.gi_max_ray_radiance.onChange([&gi](double v) {
		gi.max_ray_radiance.store(static_cast<float>(v), std::memory_order_relaxed);
	});

	p.gi_intensity.onChange([&gi](double v) { gi.intensity.store(static_cast<float>(v), std::memory_order_relaxed); });

	p.gi_debug_view.onChange([&gi](int64_t v) {
		gi.debug_view.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 6)), std::memory_order_relaxed);
	});

	p.gi_debug_mode.onChange([&gi](int64_t v) {
		gi.debug_mode.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 5)), std::memory_order_relaxed);
	});

	p.gi_backend.onChange([&gi](int64_t v) {
		gi.backend.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 1)), std::memory_order_relaxed);
	});

	p.gi_clip_cell.onChange([&gi](double v) {
		gi.clip_cell.store(std::clamp(static_cast<float>(v), 0.1f, 1.0f), std::memory_order_relaxed);
	});

	p.gi_clip_resolution.onChange([&gi](int64_t v) {
		constexpr std::array<uint32_t, 3> k_resolutions {64, 128, 192};
		gi.clip_resolution.store(k_resolutions[static_cast<size_t>(std::clamp<int64_t>(v, 0, 2))], std::memory_order_relaxed);
	});

	p.gi_clip_levels.onChange([&gi](int64_t v) {
		gi.clip_levels.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 1, 6)), std::memory_order_relaxed);
	});

	p.gi_clip_debug_view.onChange([&gi](int64_t v) {
		gi.clip_debug_view.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 6)), std::memory_order_relaxed);
	});

	p.gi_clip_debug_mode.onChange([&gi](int64_t v) {
		gi.clip_debug_mode.store(static_cast<uint32_t>(std::clamp<int64_t>(v, 0, 3)), std::memory_order_relaxed);
	});
}

auto presetDrivenValue(std::string_view key) -> std::optional<toast::params::Value> {
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
