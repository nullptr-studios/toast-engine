#include "asset_registry.hpp"

#include "types.hpp"

namespace assets {

std::unordered_map<std::string, AssetRegistry::RawLoader> AssetRegistry::s_raw;
std::unordered_map<std::string, AssetRegistry::TomlLoader> AssetRegistry::s_toml;
std::unordered_map<std::string, AssetRegistry::SchemaTomlLoader> AssetRegistry::s_schema_toml;

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

	// Plain TOML loaders
	s_toml["curve"] = [](const toml::table& t) { return Curve::fromToml(t); };

	// TOML + Schema loaders
	s_schema_toml["data"] = [](const toml::table& t, AssetHandle<Schema> s) { return std::make_unique<Data>(t, std::move(s)); };

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
