#include "material_instance.hpp"

#include "assets.hpp"

#include <sstream>
#include <toast/log.hpp>

namespace assets {

namespace {

constexpr int k_max_parent_depth = 16;
thread_local int s_parent_depth = 0;

struct DepthGuard {
	DepthGuard() { ++s_parent_depth; }

	~DepthGuard() { --s_parent_depth; }

	[[nodiscard]]
	auto exceeded() const -> bool {
		return s_parent_depth > k_max_parent_depth;
	}
};

auto readParentUID(const DataValue& root) -> toast::UID {
	if (!root.contains("material")) {
		return toast::UID(uint64_t {0});
	}
	const DataValue& v = root["material"];
	if (v.type() == DataType::asset_t) {
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

MaterialInstance::MaterialInstance(const toml::table& table, AssetHandle<Schema> schema) : Material(table, std::move(schema)) { }

MaterialInstance::~MaterialInstance() = default;

void MaterialInstance::reload(const toml::table& table) {
	Material::reload(table);
	m_parent_dirty = true;
	m_merged_objects.clear();
}

auto MaterialInstance::parent() const -> const AssetHandle<Material>& {
	if (m_parent_dirty) {
		m_parent_dirty = false;
		m_merged_objects.clear();
		const toast::UID uid = readParentUID(m_root);
		m_parent = uid.data() != 0 ? assets::load<Material>(uid) : AssetHandle<Material> {};
	}
	return m_parent;
}

auto MaterialInstance::parentMaterial() const -> Material* {
	const auto& handle = parent();
	return handle.hasValue() ? const_cast<Material*>(&handle.get()) : nullptr;
}

auto MaterialInstance::shaders() const -> const std::vector<AssetHandle<Shader>>& {
	const DepthGuard guard;
	if (guard.exceeded()) {
		TOAST_ERROR("Render", "Material instance parent chain too deep (cycle?)");
		return Material::shaders();
	}
	if (Material* p = parentMaterial()) {
		return p->shaders();
	}
	return Material::shaders();
}

auto MaterialInstance::settings() const -> MaterialSettings {
	const DepthGuard guard;
	if (guard.exceeded()) {
		TOAST_ERROR("Render", "Material instance parent chain too deep (cycle?)");
		return {};
	}

	// Parent settings as the base, instance [settings] keys override on top
	MaterialSettings base = parentMaterial() != nullptr ? parentMaterial()->settings() : MaterialSettings {};
	if (!m_root.contains("settings")) {
		return base;
	}
	const MaterialSettings own = Material::settings();
	const DataValue& s = m_root["settings"];
	if (!s.isObject()) {
		return base;
	}
	if (s.contains("blend_mode")) {
		base.blend_mode = own.blend_mode;
	}
	if (s.contains("depth_test")) {
		base.depth_test = own.depth_test;
	}
	if (s.contains("depth_write")) {
		base.depth_write = own.depth_write;
	}
	if (s.contains("cull_mode")) {
		base.cull_mode = own.cull_mode;
	}
	return base;
}

auto MaterialInstance::value(std::string_view key) const -> const DataValue* {
	const DepthGuard guard;
	if (guard.exceeded()) {
		TOAST_ERROR("Render", "Material instance parent chain too deep (cycle?)");
		return nullptr;
	}

	const DataValue* own = Material::value(key);
	Material* p = parentMaterial();
	const DataValue* inherited = p != nullptr ? p->value(key) : nullptr;

	if (own == nullptr) {
		return inherited;
	}
	if (inherited == nullptr || !own->isObject() || !inherited->isObject()) {
		return own;
	}

	// Field-level merge for object parameters
	auto [it, _] = m_merged_objects.insert_or_assign(std::string(key), *inherited);
	DataValue& merged = it->second;
	for (const auto& [field_key, field_value] : own->items()) {
		merged.set(field_key, field_value);
	}
	return &merged;
}

auto MaterialInstance::rootMaterial() -> Material* {
	const DepthGuard guard;
	if (guard.exceeded()) {
		TOAST_ERROR("Render", "Material instance parent chain too deep (cycle?)");
		return this;
	}
	if (Material* p = parentMaterial()) {
		return p->rootMaterial();
	}
	return this;
}

auto MaterialInstance::deltaRoot() const -> DataValue {
	auto out = DataValue::makeObject();
	Material* p = parentMaterial();

	for (const auto& [key, v] : m_root.items()) {
		if (key == "material") {
			out.set(key, v);
			continue;
		}

		const DataValue* pv = p != nullptr ? p->value(key) : nullptr;
		if (pv == nullptr) {
			out.set(key, v);
			continue;
		}

		if (v.isObject() && pv->isObject()) {
			auto obj = DataValue::makeObject();
			for (const auto& [field_key, field_value] : v.items()) {
				const DataValue* pf = pv->contains(field_key) ? &(*pv)[field_key] : nullptr;
				if (pf == nullptr || field_value.toJson() != pf->toJson()) {
					obj.set(field_key, field_value);
				}
			}
			if (!obj.empty()) {
				out.set(key, std::move(obj));
			}
			continue;
		}

		if (v.toJson() != pv->toJson()) {
			out.set(key, v);
		}
	}

	return out;
}

auto MaterialInstance::serialize(SaveMode mode) const -> std::vector<uint8_t> {
	const DataValue delta = deltaRoot();

	if (mode == SaveMode::game) {
		auto j = delta.toJson();
		if (m_schema.hasValue()) {
			j["schema"] = m_schema.uid().get();
		}
		auto bson = json_t::to_bson(j);
		return bson;
	}

	toml::table tbl;
	if (m_schema.hasValue()) {
		tbl.insert_or_assign("schema", m_schema.uid().get());
	}
	if (delta.type() == DataType::object_t) {
		for (const auto& [k, v] : delta.items()) {
			v.appendTo(tbl, k);
		}
	}

	std::ostringstream ss;
	ss << tbl;
	auto str = ss.str();
	return {str.begin(), str.end()};
}

}
