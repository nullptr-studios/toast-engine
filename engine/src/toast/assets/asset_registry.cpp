#include "asset_registry.hpp"

#include "types.hpp"

namespace assets {

std::unordered_map<std::string, AssetRegistry::RawLoader> AssetRegistry::s_raw;
std::unordered_map<std::string, AssetRegistry::TomlLoader> AssetRegistry::s_toml;
std::unordered_map<std::string, AssetRegistry::SchemaTomlLoader> AssetRegistry::s_schema_toml;
std::unordered_map<std::string, std::string> AssetRegistry::s_lua_names;

void AssetRegistry::init() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;

	// Raw binary loaders
	s_raw["mesh"] = [](const std::vector<uint8_t>& d) { return std::make_unique<Mesh>(d); };
	s_raw["texture"] = [](std::vector<uint8_t> d) { return std::make_unique<Texture>(std::move(d)); };
	s_raw["schema"] = [](std::vector<uint8_t> d) {
		std::string_view json(reinterpret_cast<const char*>(d.data()), d.size());
		return std::make_unique<Schema>(json);
	};
	s_raw["audio_bank"] = [](std::vector<uint8_t> d) { return std::make_unique<AudioBank>(std::move(d)); };
	s_raw["audio_strings"] = [](std::vector<uint8_t> d) { return std::make_unique<AudioStrings>(std::move(d)); };
	s_raw["script"] = [](std::vector<uint8_t> d) { return std::make_unique<Script>(std::move(d)); };
	s_raw["ui_element"] = [](std::vector<uint8_t> d) { return std::make_unique<UIElement>(std::move(d)); };
	s_raw["ui_style"] = [](std::vector<uint8_t> d) { return std::make_unique<UIStyle>(std::move(d)); };
	s_raw["font"] = [](std::vector<uint8_t> d) { return std::make_unique<Font>(std::move(d)); };
	s_raw["ui_image"] = [](std::vector<uint8_t> d) { return std::make_unique<UIImage>(std::move(d)); };

	// Plain TOML loaders
	s_toml["curve"] = [](const toml::table& t) { return Curve::fromToml(t); };

	// TOML + Schema loaders
	s_schema_toml["data"] = [](const toml::table& t, AssetHandle<Schema> s) { return std::make_unique<Data>(t, std::move(s)); };
	s_schema_toml["material"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<Material>(t, std::move(s));
	};

	// Input assets
	s_schema_toml["haptic"] = [](const toml::table& t, AssetHandle<Schema> s) { return std::make_unique<Haptic>(t, std::move(s)); };
	s_schema_toml["input_action"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<Action>(t, std::move(s));
	};
	s_schema_toml["input_layout"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<InputLayout>(t, std::move(s));
	};
	s_schema_toml["input_settings"] = [](const toml::table& t, const AssetHandle<Schema>& s) {
		return std::make_unique<InputSettings>(t, s);
	};
	s_schema_toml["audio_event"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<AudioEvent>(t, std::move(s));
	};
	s_schema_toml["audio_bus"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<AudioBus>(t, std::move(s));
	};
	s_schema_toml["audio_port"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<AudioPort>(t, std::move(s));
	};
	s_schema_toml["audio_snapshot"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<AudioSnapshot>(t, std::move(s));
	};
	s_schema_toml["audio_vca"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<AudioVca>(t, std::move(s));
	};
	s_schema_toml["font_family"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<FontFamily>(t, std::move(s));
	};
	s_schema_toml["color_scheme"] = [](const toml::table& t, AssetHandle<Schema> s) {
		return std::make_unique<ColorScheme>(t, std::move(s));
	};

	// Lua global names
	s_lua_names["mesh"] = "Mesh";
	s_lua_names["texture"] = "Texture";
	s_lua_names["data"] = "Data";
	s_lua_names["material"] = "Material";
	s_lua_names["schema"] = "Schema";
	s_lua_names["node"] = "Prefab";
	s_lua_names["script"] = "Script";
	s_lua_names["curve"] = "Curve";
	s_lua_names["audio_strings"] = "AudioStrings";
	s_lua_names["audio_bank"] = "AudioBank";
	s_lua_names["haptic"] = "Haptic";
	s_lua_names["input_action"] = "Action";
	s_lua_names["input_layout"] = "InputLayout";
	s_lua_names["input_settings"] = "InputSettings";
	s_lua_names["audio_event"] = "AudioEvent";
	s_lua_names["audio_bus"] = "AudioBus";
	s_lua_names["audio_port"] = "AudioPort";
	s_lua_names["audio_snapshot"] = "AudioSnapshot";
	s_lua_names["audio_vca"] = "AudioVca";
	s_lua_names["ui_element"] = "UIElement";
	s_lua_names["ui_style"] = "UIStyle";
	s_lua_names["font"] = "Font";
	s_lua_names["ui_image"] = "UIImage";
	s_lua_names["font_family"] = "FontFamily";
	s_lua_names["color_scheme"] = "ColorScheme";
}

void AssetRegistry::registerLuaName(std::string_view type, std::string_view lua_name) {
	s_lua_names[std::string(type)] = std::string(lua_name);
}

auto AssetRegistry::registeredLuaNames() -> const std::unordered_map<std::string, std::string>& {
	return s_lua_names;
}

void AssetRegistry::registerRaw(std::string_view type, RawLoader loader) {
	s_raw[std::string(type)] = std::move(loader);
}

void AssetRegistry::registerToml(std::string_view type, TomlLoader loader) {
	s_toml[std::string(type)] = std::move(loader);
}

void AssetRegistry::registerSchemaToml(std::string_view type, SchemaTomlLoader loader) {
	s_schema_toml[std::string(type)] = std::move(loader);
}

auto AssetRegistry::hasRaw(std::string_view type) -> bool {
	return s_raw.contains(std::string(type));
}

auto AssetRegistry::hasToml(std::string_view type) -> bool {
	return s_toml.contains(std::string(type));
}

auto AssetRegistry::hasSchemaToml(std::string_view type) -> bool {
	return s_schema_toml.contains(std::string(type));
}

auto AssetRegistry::createRaw(std::string_view type, std::vector<uint8_t> data) -> std::unique_ptr<Asset> {
	auto it = s_raw.find(std::string(type));
	if (it != s_raw.end()) {
		return it->second(std::move(data));
	}
	return nullptr;
}

auto AssetRegistry::createToml(std::string_view type, toml::table table) -> std::unique_ptr<Asset> {
	auto it = s_toml.find(std::string(type));
	if (it != s_toml.end()) {
		return it->second(std::move(table));
	}
	return nullptr;
}

auto AssetRegistry::createSchemaToml(std::string_view type, toml::table table, AssetHandle<Schema> schema)
    -> std::unique_ptr<Asset> {
	auto it = s_schema_toml.find(std::string(type));
	if (it != s_schema_toml.end()) {
		return it->second(std::move(table), std::move(schema));
	}
	return nullptr;
}

}
