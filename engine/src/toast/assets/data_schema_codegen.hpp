/**
 * @file data_schema_codegen.hpp
 * @author Xein
 * @date 28 Sep 2026
 * @brief Generates lua docs for scemas
 */

#pragma once

#include "core_types.hpp"

#include <filesystem>
#include <string>
#include <toast/uid.hpp>
#include <vector>

namespace assets {

class Schema;

struct NamedSchemaEntry {
	toast::UID uid;
	std::string name;    // PascalCase
	Handle<Schema> handle;
};

/**
 * Enumerates every valid `schema` asset
 */
auto namedSchemaEntries() -> std::vector<NamedSchemaEntry>;

/**
 * Regenerates the schemas' lua file
 */
auto generateDataSchemaLuaStubs(const std::filesystem::path& out_path) -> bool;

}
