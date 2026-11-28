#include "toast/renderer/settings/post_process_params.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <mutex>
#include <toast/assets/asset_manager.hpp>
#include <toast/assets/assets.hpp>

namespace renderer {
namespace {

using Kind = PostProcessParams::Kind;
using Slot = PostProcessParams::Slot;
using ParamValue = PostProcessParams::ParamValue;

constexpr std::string_view k_core_schema = "core://schemas/post_process.schema.json";

auto normalize(const Slot& slot, float x) -> float {
	switch (slot.kind) {
		case Kind::boolean: return x != 0.0f ? 1.0f : 0.0f;
		case Kind::enumeration:
			return std::clamp(std::round(x), 0.0f, static_cast<float>(std::max<size_t>(slot.options.size(), 1) - 1));
		case Kind::integer: return std::clamp(std::round(x), slot.min, slot.max);
		default: return std::clamp(x, slot.min, slot.max);
	}
}

auto optionIndex(const Slot& slot, std::string_view option) -> std::optional<uint32_t> {
	const auto it = std::ranges::find(slot.options, option);
	return it == slot.options.end() ? std::nullopt : std::optional<uint32_t> {static_cast<uint32_t>(it - slot.options.begin())};
}

auto lookup(const assets::DataValue& root, std::string_view path) -> const assets::DataValue* {
	const assets::DataValue* node = &root;
	while (true) {
		const auto dot = path.find('.');
		const auto key = path.substr(0, dot);
		if (!node->isObject() || !node->contains(key)) {
			return nullptr;
		}
		node = &(*node)[key];
		if (dot == std::string_view::npos) {
			return node;
		}
		path.remove_prefix(dot + 1);
	}
}

auto readValue(const Slot& slot, const assets::DataValue& value) -> std::optional<ParamValue> {
	using assets::DataType;
	ParamValue out;
	switch (slot.kind) {
		case Kind::number:
		case Kind::integer:
			if (value.type() == DataType::float_t) {
				out.v.x = value.as<float>();
			} else if (value.type() == DataType::int_t) {
				out.v.x = static_cast<float>(value.as<int64_t>());
			} else {
				return std::nullopt;
			}
			break;
		case Kind::boolean:
			if (value.type() != DataType::bool_t) {
				return std::nullopt;
			}
			out.v.x = value.as<bool>() ? 1.0f : 0.0f;
			break;
		case Kind::enumeration: {
			if (value.type() != DataType::string_t) {
				return std::nullopt;
			}
			const auto index = optionIndex(slot, value.as<std::string_view>());
			if (!index) {
				return std::nullopt;
			}
			out.v.x = static_cast<float>(*index);
			break;
		}
		case Kind::color:
			if (value.type() != DataType::color3_t && value.type() != DataType::vec3_t) {
				return std::nullopt;
			}
			out.v = value.as<glm::vec3>();
			break;
		case Kind::asset:
			if (value.type() != DataType::asset_t) {
				return std::nullopt;
			}
			out.asset = value.as<toast::UID>();
			break;
	}
	if (slot.kind != Kind::color && slot.kind != Kind::asset) {
		out.v.x = normalize(slot, out.v.x);
	}
	return out;
}

void collectSlots(const std::vector<assets::SchemaField>& fields, const std::string& prefix, PostProcessParams::Layout& layout) {
	using assets::DataType;
	for (const auto& field : fields) {
		const std::string name = prefix.empty() ? field.name : prefix + "." + field.name;
		if (field.type == DataType::object_t) {
			collectSlots(field.children, name, layout);
			continue;
		}

		Slot slot;
		slot.name = name;
		switch (field.type) {
			case DataType::float_t: slot.kind = Kind::number; break;
			case DataType::int_t: slot.kind = Kind::integer; break;
			case DataType::bool_t: slot.kind = Kind::boolean; break;
			case DataType::string_t:
				if (field.enum_options.empty()) {
					continue;
				}
				slot.kind = Kind::enumeration;
				slot.options = field.enum_options;
				break;
			case DataType::color3_t:
			case DataType::vec3_t: slot.kind = Kind::color; break;
			case DataType::asset_t: slot.kind = Kind::asset; break;
			default: continue;
		}
		if (field.min) {
			slot.min = static_cast<float>(*field.min);
		}
		if (field.max) {
			slot.max = static_cast<float>(*field.max);
		}
		if (field.default_value) {
			if (const auto value = readValue(slot, *field.default_value)) {
				slot.fallback = *value;
			}
		}
		layout.add(std::move(slot));
	}
}

}

auto PostProcessParams::Layout::find(std::string_view name) const -> std::optional<uint32_t> {
	const auto it = m_index.find(name);
	return it == m_index.end() ? std::nullopt : std::optional<uint32_t> {it->second};
}

void PostProcessParams::Layout::add(Slot slot) {
	m_index.emplace(slot.name, static_cast<uint32_t>(slots.size()));
	slots.push_back(std::move(slot));
}

auto PostProcessParams::Layout::fromSchema(const assets::Schema& schema) -> std::shared_ptr<const Layout> {
	auto layout = std::make_shared<Layout>();
	collectSlots(schema.fields(), {}, *layout);
	return layout;
}

auto PostProcessParams::coreLayout() -> std::shared_ptr<const Layout> {
	static std::mutex mutex;
	static std::shared_ptr<const Layout> cached;
	static const auto empty = std::make_shared<const Layout>();

	const std::lock_guard lock(mutex);
	if (cached) {
		return cached;
	}
	if (!assets::AssetManager::isInitialized()) {
		return empty;
	}

	const auto schema = assets::load<assets::Schema>(k_core_schema);
	if (!schema.hasValue() || !schema->isValid()) {
		static bool warned = false;
		if (!std::exchange(warned, true)) {
			TOAST_ERROR("PostProcess", "Could not load {}, post-process parameters fall back to struct defaults", k_core_schema);
		}
		return empty;
	}
	cached = Layout::fromSchema(*schema);
	return cached;
}

PostProcessParams::PostProcessParams(std::shared_ptr<const Layout> layout) : m_layout(std::move(layout)) {
	reset();
}

void PostProcessParams::reset() {
	// In place, so a thread reading while this runs never sees the vector change size
	m_values.resize(m_layout->slots.size());
	for (size_t i = 0; i < m_values.size(); ++i) {
		m_values[i] = m_layout->slots[i].fallback;
	}
}

void PostProcessParams::loadFrom(const assets::Data& profile) {
	reset();
	for (size_t i = 0; i < m_layout->slots.size(); ++i) {
		const auto& slot = m_layout->slots[i];
		if (const auto* value = lookup(profile.root(), slot.name)) {
			if (const auto parsed = readValue(slot, *value)) {
				m_values[i] = *parsed;
			}
		}
	}
}

void PostProcessParams::blend(const PostProcessParams& source, float weight) {
	if (m_layout != source.m_layout) {
		return;
	}
	const float t = std::clamp(weight, 0.0f, 1.0f);
	if (t <= 0.0f) {
		return;
	}

	for (size_t i = 0; i < m_values.size(); ++i) {
		const auto& slot = m_layout->slots[i];
		auto& target = m_values[i];
		const auto& from = source.m_values[i];
		switch (slot.kind) {
			case Kind::number:
			case Kind::color: target.v = glm::mix(target.v, from.v, t); break;
			case Kind::integer: target.v.x = std::round(glm::mix(target.v.x, from.v.x, t)); break;
			case Kind::boolean:
			case Kind::enumeration:
			case Kind::asset:
				if (t >= 0.5f) {
					target = from;
				}
				break;
		}
	}
}

auto PostProcessParams::setNumber(std::string_view name, float value) -> bool {
	const auto index = m_layout->find(name);
	if (!index) {
		return false;
	}
	const auto& slot = m_layout->slots[*index];
	if (slot.kind == Kind::color || slot.kind == Kind::asset) {
		return false;
	}
	m_values[*index].v.x = normalize(slot, value);
	return true;
}

auto PostProcessParams::setColor(std::string_view name, const glm::vec3& value) -> bool {
	const auto index = m_layout->find(name);
	if (!index || m_layout->slots[*index].kind != Kind::color) {
		return false;
	}
	m_values[*index].v = value;
	return true;
}

auto PostProcessParams::setOption(std::string_view name, std::string_view option) -> bool {
	const auto index = m_layout->find(name);
	if (!index || m_layout->slots[*index].kind != Kind::enumeration) {
		return false;
	}
	const auto choice = optionIndex(m_layout->slots[*index], option);
	if (!choice) {
		return false;
	}
	m_values[*index].v.x = static_cast<float>(*choice);
	return true;
}

auto PostProcessParams::number(std::string_view name) const -> std::optional<float> {
	const auto index = m_layout->find(name);
	if (!index || m_layout->slots[*index].kind == Kind::color || m_layout->slots[*index].kind == Kind::asset) {
		return std::nullopt;
	}
	return m_values[*index].v.x;
}

auto PostProcessParams::color(std::string_view name) const -> std::optional<glm::vec3> {
	const auto index = m_layout->find(name);
	if (!index || m_layout->slots[*index].kind != Kind::color) {
		return std::nullopt;
	}
	return m_values[*index].v;
}

auto PostProcessParams::assetUid(std::string_view name) const -> std::optional<toast::UID> {
	const auto index = m_layout->find(name);
	if (!index || m_layout->slots[*index].kind != Kind::asset) {
		return std::nullopt;
	}
	return m_values[*index].asset;
}

auto PostProcessParams::resolve() const -> PostProcessSettings {
	PostProcessSettings s;

	const auto f = [this](std::string_view name, float& out) {
		if (const auto v = number(name)) {
			out = *v;
		}
	};
	const auto u = [this](std::string_view name, uint32_t& out) {
		if (const auto v = number(name)) {
			out = static_cast<uint32_t>(std::max(*v, 0.0f));
		}
	};
	const auto b = [this](std::string_view name, bool& out) {
		if (const auto v = number(name)) {
			out = *v != 0.0f;
		}
	};
	const auto c = [this](std::string_view name, float& r, float& g, float& bl) {
		if (const auto v = color(name)) {
			r = v->r;
			g = v->g;
			bl = v->b;
		}
	};

	f("bloom.threshold", s.bloom.threshold);
	f("bloom.knee", s.bloom.knee);
	f("bloom.filter_radius", s.bloom.filter_radius);
	f("bloom.strength", s.bloom.strength);

	f("lens_flare.intensity", s.lens_flare.intensity);
	f("lens_flare.threshold", s.lens_flare.threshold);
	f("lens_flare.threshold_range", s.lens_flare.threshold_range);
	f("lens_flare.ghost_intensity", s.lens_flare.ghost_intensity);
	f("lens_flare.chroma_shift", s.lens_flare.chroma_shift);
	f("lens_flare.halo_intensity", s.lens_flare.halo_intensity);
	f("lens_flare.halo_width", s.lens_flare.halo_width);
	f("lens_flare.halo_mask", s.lens_flare.halo_mask);
	f("lens_flare.halo_compression", s.lens_flare.halo_compression);
	f("lens_flare.glare_intensity", s.lens_flare.glare_intensity);
	f("lens_flare.glare_divider", s.lens_flare.glare_divider);
	f("lens_flare.anamorphic_intensity", s.lens_flare.anamorphic_intensity);
	f("lens_flare.anamorphic_tint", s.lens_flare.anamorphic_tint);
	f("lens_flare.dirt_intensity", s.lens_flare.dirt_intensity);
	f("lens_flare.halo_thickness", s.lens_flare.halo_thickness);
	f("lens_flare.halo_dispersion", s.lens_flare.halo_dispersion);
	c("lens_flare.halo_tint", s.lens_flare.halo_tint_r, s.lens_flare.halo_tint_g, s.lens_flare.halo_tint_b);
	if (const auto uid = assetUid("lens_flare.dirt_texture"); uid && uid->data() != 0) {
		s.lens_flare.dirt_texture = assets::load<assets::Texture>(*uid);
	}

	f("tonemap.exposure", s.tonemap.exposure);
	u("tonemap.mode", s.tonemap.mode);
	f("tonemap.gt_contrast_dip", s.tonemap.gt_contrast_dip);
	f("tonemap.chromatic_aberration", s.tonemap.chromatic_aberration);
	f("tonemap.gamma", s.tonemap.gamma);
	f("tonemap.contrast", s.tonemap.contrast);
	f("tonemap.saturation", s.tonemap.saturation);
	f("tonemap.vignette", s.tonemap.vignette);
	f("tonemap.grain", s.tonemap.grain);
	b("tonemap.auto_exposure", s.tonemap.auto_exposure);
	f("tonemap.auto_exposure_min", s.tonemap.auto_exposure_min);
	f("tonemap.auto_exposure_max", s.tonemap.auto_exposure_max);
	f("tonemap.adapt_speed_up", s.tonemap.adapt_speed_up);
	f("tonemap.adapt_speed_down", s.tonemap.adapt_speed_down);

	auto& grade = s.color_grading;
	f("color_grading.temperature", grade.temperature);
	f("color_grading.tint", grade.tint);
	c("color_grading.color_filter", grade.color_filter_r, grade.color_filter_g, grade.color_filter_b);
	f("color_grading.hue_shift", grade.hue_shift);
	c("color_grading.lift", grade.lift_r, grade.lift_g, grade.lift_b);
	f("color_grading.lift_offset", grade.lift_offset);
	c("color_grading.gamma", grade.gamma_r, grade.gamma_g, grade.gamma_b);
	f("color_grading.gamma_offset", grade.gamma_offset);
	c("color_grading.gain", grade.gain_r, grade.gain_g, grade.gain_b);
	f("color_grading.gain_offset", grade.gain_offset);
	if (const auto uid = assetUid("color_grading.lut"); uid && uid->data() != 0) {
		grade.lut = assets::load<assets::Texture>(*uid);
	}
	f("color_grading.lut_intensity", grade.lut_intensity);

	f("fxaa.contrast_threshold", s.fxaa.contrast_threshold);
	f("fxaa.relative_threshold", s.fxaa.relative_threshold);
	f("fxaa.subpixel_blending", s.fxaa.subpixel_blending);

	f("ssr.intensity", s.ssr.intensity);
	f("ssr.max_roughness", s.ssr.max_roughness);
	f("ssr.thickness", s.ssr.thickness);
	f("ssr.stride", s.ssr.stride);
	u("ssr.max_steps", s.ssr.max_steps);

	f("ssao.radius", s.ssao.radius);
	f("ssao.strength", s.ssao.strength);
	f("ssao.range_cutoff", s.ssao.range_cutoff);
	u("ssao.sample_count", s.ssao.sample_count);

	f("fog.density", s.fog.density);
	f("fog.height", s.fog.height);
	f("fog.height_falloff", s.fog.height_falloff);
	c("fog.albedo", s.fog.albedo_r, s.fog.albedo_g, s.fog.albedo_b);
	f("fog.anisotropy", s.fog.anisotropy);
	f("fog.ambient", s.fog.ambient);
	f("fog.distance", s.fog.distance);

	return s;
}

}
