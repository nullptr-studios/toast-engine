#include "material.hpp"

#include "assets.hpp"

namespace assets {

namespace {

auto readAssetUID(const DataValue& v) -> toast::UID {
	if (v.type() == DataType::asset_t || v.type() == DataType::node_t) {
		return static_cast<toast::UID>(v);
	}
	if (v.type() == DataType::string_t) {
		const auto s = static_cast<std::string>(v);
		if (s.size() == 11) {
			return {toast::UID::fromString(s)};
		}
	}
	return toast::UID(uint64_t {0});
}

}

Material::Material(const toml::table& table, AssetHandle<Schema> schema) : Data(table, std::move(schema), Data::keep_all_keys) { }

Material::~Material() = default;

auto Material::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	return Data::serialize(mode);
}

void Material::reload(const toml::table& table) {
	Data::reload(table);
	m_shaders_dirty = true;
}

auto Material::name() const -> std::string {
	if (m_root.contains("name")) {
		if (auto n = m_root["name"].value<std::string>()) {
			return *n;
		}
	}
	return "Unnamed Material";
}

auto Material::shaders() const -> const std::vector<AssetHandle<Shader>>& {
	if (!m_shaders_dirty) {
		return m_shader_handles;
	}
	m_shaders_dirty = false;
	m_shader_handles.clear();

	if (!m_root.contains("shaders")) {
		return m_shader_handles;
	}

	const DataValue& list = m_root["shaders"];
	if (!list.isArray()) {
		return m_shader_handles;
	}

	for (size_t i = 0; i < list.size(); ++i) {
		const toast::UID uid = readAssetUID(list[i]);
		if (uid.data() == 0) {
			continue;
		}
		m_shader_handles.push_back(assets::load<Shader>(uid));
	}
	return m_shader_handles;
}

auto Material::settings() const -> MaterialSettings {
	MaterialSettings result;
	if (!m_root.contains("settings")) {
		return result;
	}

	const DataValue& s = m_root["settings"];
	if (!s.isObject()) {
		return result;
	}

	if (s.contains("blend_mode")) {
		if (auto mode = s["blend_mode"].value<std::string>()) {
			if (*mode == "alpha") {
				result.blend_mode = BlendMode::alpha;
			} else if (*mode == "additive") {
				result.blend_mode = BlendMode::additive;
			} else if (*mode == "multiply") {
				result.blend_mode = BlendMode::multiply;
			}
		}
	}
	if (s.contains("depth_test")) {
		result.depth_test = s["depth_test"].value<bool>().value_or(true);
	}
	if (s.contains("depth_write")) {
		result.depth_write = s["depth_write"].value<bool>().value_or(true);
	}
	if (s.contains("cull_mode")) {
		if (auto mode = s["cull_mode"].value<std::string>()) {
			if (*mode == "none") {
				result.cull_mode = CullMode::none;
			} else if (*mode == "front") {
				result.cull_mode = CullMode::front;
			}
		}
	}
	return result;
}

auto Material::value(std::string_view key) const -> const DataValue* {
	if (!m_root.contains(key)) {
		return nullptr;
	}
	return &m_root[key];
}

}
