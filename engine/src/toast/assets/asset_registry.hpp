#pragma once

#include "core_types.hpp"
#include "schema.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace assets {

class TOAST_API AssetRegistry {
public:
	using RawLoader = std::function<std::unique_ptr<Asset>(std::vector<uint8_t>)>;
	using TomlLoader = std::function<std::unique_ptr<Asset>(toml::table)>;
	using SchemaTomlLoader = std::function<std::unique_ptr<Asset>(toml::table, Handle<Schema>)>;

	static void init();

	static void registerRaw(std::string_view type, RawLoader loader);
	static void registerToml(std::string_view type, TomlLoader loader);
	static void registerSchemaToml(std::string_view type, SchemaTomlLoader loader);

	[[nodiscard]]
	static auto hasRaw(std::string_view type) -> bool;

	[[nodiscard]]
	static auto hasToml(std::string_view type) -> bool;

	[[nodiscard]]
	static auto hasSchemaToml(std::string_view type) -> bool;

	static auto createRaw(std::string_view type, std::vector<uint8_t> data) -> std::unique_ptr<Asset>;
	static auto createToml(std::string_view type, toml::table table) -> std::unique_ptr<Asset>;
	static auto createSchemaToml(std::string_view type, toml::table table, Handle<Schema> schema) -> std::unique_ptr<Asset>;

	static void registerLuaName(std::string_view type, std::string_view lua_name);

	[[nodiscard]]
	static auto registeredLuaNames() -> const std::unordered_map<std::string, std::string>&;

private:
	static std::unordered_map<std::string, RawLoader> s_raw;
	static std::unordered_map<std::string, TomlLoader> s_toml;
	static std::unordered_map<std::string, SchemaTomlLoader> s_schema_toml;
	static std::unordered_map<std::string, std::string> s_lua_names;
};

}    // namespace assets
