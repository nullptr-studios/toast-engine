#include "test_registry.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <toast/renderer/settings/post_process_params.hpp>

namespace {

/// assert() is compiled out in release builds, and the writes must still happen there
void must(bool ok) {
	assert(ok);
	(void)ok;
}

auto near(float a, float b) -> bool {
	return std::fabs(a - b) < 1e-4f;
}

auto coreSchemaLayout() -> std::shared_ptr<const renderer::PostProcessParams::Layout> {
	std::ifstream file(std::string(TOAST_CORE_ASSETS_DIR) + "/schemas/post_process.schema.json");
	std::stringstream text;
	text << file.rdbuf();
	const assets::Schema schema(text.str());
	assert(schema.isValid());
	return renderer::PostProcessParams::Layout::fromSchema(schema);
}

}

/// A volume's blend distance is only a visible fade if the values actually interpolate; the ones that cannot
/// switch at the halfway point instead. The layout comes from the shipped schema, so this also proves the schema
/// parses and that its defaults still match the struct the passes fall back to
TOAST_TEST_NAMED("postprocess", "postprocess/01-blend", test_postprocess_01_blend) {
	using renderer::PostProcessParams;
	const auto layout = coreSchemaLayout();
	assert(layout->slots.size() > 50);

	// Schema defaults are the struct defaults, so a missing profile and a missing schema grade the same
	const renderer::PostProcessSettings fallback {};
	const auto defaults = PostProcessParams(layout).resolve();
	assert(near(defaults.bloom.strength, fallback.bloom.strength));
	assert(near(defaults.lens_flare.dirt_intensity, fallback.lens_flare.dirt_intensity));
	assert(near(defaults.lens_flare.halo_tint_g, fallback.lens_flare.halo_tint_g));
	assert(near(defaults.tonemap.gamma, fallback.tonemap.gamma));
	assert(defaults.tonemap.mode == fallback.tonemap.mode);
	assert(defaults.ssr.max_steps == fallback.ssr.max_steps);
	assert(defaults.ssao.sample_count == fallback.ssao.sample_count);
	assert(near(defaults.fog.distance, fallback.fog.distance));
	assert(near(defaults.fog.albedo_b, fallback.fog.albedo_b));
	assert(near(defaults.color_grading.temperature, fallback.color_grading.temperature));
	assert(near(defaults.color_grading.color_filter_g, 1.0f));
	assert(near(defaults.color_grading.gain_b, fallback.color_grading.gain_b));
	assert(near(defaults.color_grading.lut_intensity, 1.0f));
	assert(!defaults.color_grading.lut.hasValue());

	// Grading sliders fade between volumes like every other float and colour
	PostProcessParams ungraded(layout);
	PostProcessParams graded(layout);
	must(graded.setNumber("color_grading.temperature", 1.0f));
	must(graded.setColor("color_grading.lift", {0.0f, 1.0f, 1.0f}));
	ungraded.blend(graded, 0.5f);
	assert(near(ungraded.resolve().color_grading.temperature, 0.5f));
	assert(near(ungraded.resolve().color_grading.lift_r, 0.5f));

	PostProcessParams target(layout);
	PostProcessParams source(layout);
	must(target.setNumber("tonemap.exposure", 1.0f));
	must(source.setNumber("tonemap.exposure", 3.0f));
	must(target.setNumber("ssao.sample_count", 16.0f));
	must(source.setNumber("ssao.sample_count", 32.0f));
	must(target.setOption("tonemap.mode", "Reinhard"));
	must(source.setOption("tonemap.mode", "ACES"));
	must(source.setColor("fog.albedo", {0.0f, 1.0f, 0.5f}));

	// Weight 0 leaves the target exactly as it was - a volume the camera cannot reach contributes nothing
	PostProcessParams zero = target;
	zero.blend(source, 0.0f);
	assert(near(zero.resolve().tonemap.exposure, 1.0f));
	assert(zero.resolve().tonemap.mode == 0);

	// Half way is half way for a float, and for a color
	PostProcessParams half = target;
	half.blend(source, 0.5f);
	assert(near(half.resolve().tonemap.exposure, 2.0f));
	assert(half.resolve().ssao.sample_count == 24);
	assert(near(half.resolve().fog.albedo_r, 0.5f));

	// And the curve switches at exactly that point rather than landing between two curves that do not exist
	assert(half.resolve().tonemap.mode == 1);

	PostProcessParams just_under = target;
	just_under.blend(source, 0.49f);
	assert(just_under.resolve().tonemap.mode == 0);

	// Full weight replaces
	PostProcessParams full = target;
	full.blend(source, 1.0f);
	assert(near(full.resolve().tonemap.exposure, 3.0f));
	assert(full.resolve().ssao.sample_count == 32);

	// Out-of-range weights are clamped rather than extrapolating past the source
	PostProcessParams over = target;
	over.blend(source, 4.0f);
	assert(near(over.resolve().tonemap.exposure, 3.0f));

	// Runtime writes are clamped to the schema range and refused for names or kinds that do not fit
	must(target.setNumber("bloom.strength", 5.0f));
	assert(near(target.resolve().bloom.strength, 1.0f));
	assert(!target.setNumber("bloom.nonsense", 1.0f));
	assert(!target.setNumber("fog.albedo", 1.0f));
	assert(!target.setColor("bloom.strength", {1.0f, 1.0f, 1.0f}));
	assert(!target.setOption("tonemap.mode", "Filmic"));
	assert(target.number("bloom.strength").value_or(0.0f) == 1.0f);
}
