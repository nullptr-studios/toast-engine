#include "asset_registry.hpp"

#include "types.hpp"

namespace assets {

std::unordered_map<std::string, AssetRegistry::RawLoader> AssetRegistry::s_raw;
std::unordered_map<std::string, AssetRegistry::TomlLoader> AssetRegistry::s_toml;
std::unordered_map<std::string, AssetRegistry::SchemaTomlLoader> AssetRegistry::s_schemaToml;

void AssetRegistry::init() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;

	// Raw binary loaders
	s_raw["texture"] = [](std::vector<uint8_t> d) { return std::make_unique<Texture>(std::move(d)); };
	s_raw["schema"] = [](std::vector<uint8_t> d) {
		std::string_view json(reinterpret_cast<const char*>(d.data()), d.size());
		return std::make_unique<Schema>(json);
	};
	s_raw["audio_bank"] = [](std::vector<uint8_t> d) { return std::make_unique<AudioBank>(std::move(d)); };
	s_raw["audio_strings"] = [](std::vector<uint8_t> d) { return std::make_unique<AudioStrings>(std::move(d)); };

	// Plain TOML loaders
	s_toml["curve"] = [](toml::table t) { return Curve::fromToml(std::move(t)); };

	// TOML + Schema loaders
	s_schemaToml["data"] = [](toml::table t, AssetHandle<Schema> s) { return std::make_unique<Data>(std::move(t), std::move(s)); };
	s_schemaToml["audio_event"] = [](toml::table t, AssetHandle<Schema> s) {
		return std::make_unique<AudioEvent>(std::move(t), std::move(s));
	};
	s_schemaToml["audio_bus"] = [](toml::table t, AssetHandle<Schema> s) {
		return std::make_unique<AudioBus>(std::move(t), std::move(s));
	};
	s_schemaToml["audio_port"] = [](toml::table t, AssetHandle<Schema> s) {
		return std::make_unique<AudioPort>(std::move(t), std::move(s));
	};
	s_schemaToml["audio_snapshot"] = [](toml::table t, AssetHandle<Schema> s) {
		return std::make_unique<AudioSnapshot>(std::move(t), std::move(s));
	};
	s_schemaToml["audio_vca"] = [](toml::table t, AssetHandle<Schema> s) {
		return std::make_unique<AudioVca>(std::move(t), std::move(s));
	};
}

void AssetRegistry::registerRaw(std::string_view type, RawLoader loader) {
	s_raw[std::string(type)] = std::move(loader);
}

void AssetRegistry::registerToml(std::string_view type, TomlLoader loader) {
	s_toml[std::string(type)] = std::move(loader);
}

void AssetRegistry::registerSchemaToml(std::string_view type, SchemaTomlLoader loader) {
	s_schemaToml[std::string(type)] = std::move(loader);
}

bool AssetRegistry::hasRaw(std::string_view type) {
	return s_raw.contains(std::string(type));
}

bool AssetRegistry::hasToml(std::string_view type) {
	return s_toml.contains(std::string(type));
}

bool AssetRegistry::hasSchemaToml(std::string_view type) {
	return s_schemaToml.contains(std::string(type));
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
	auto it = s_schemaToml.find(std::string(type));
	if (it != s_schemaToml.end()) {
		return it->second(std::move(table), std::move(schema));
	}
	return nullptr;
}

}
