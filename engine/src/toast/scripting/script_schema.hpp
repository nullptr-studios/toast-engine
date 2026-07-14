/**
 * @file script_schema.hpp
 * @author Xein
 * @date 13 Jul 2026
 *
 * @brief Description of a script's exported variables for the inspector
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace scripting {

enum class LuaVarKind : uint8_t {
	boolean,
	integer,
	number,
	string,
	vec2,
	vec3,
	vec4,
	color3,
	color4,
	node_ref,
	asset_ref,
};

struct LuaVarDesc {
	std::string name;
	std::string path;
	LuaVarKind kind = LuaVarKind::boolean;
	bool is_array = false;
	std::string ref_type;
};

struct LuaSubgroup {
	std::string name;
	std::vector<LuaVarDesc> fields;
};

struct LuaGroup {
	std::string name;
	std::vector<LuaVarDesc> fields;
	std::vector<LuaSubgroup> subgroups;
};

struct ScriptSchema {
	std::vector<LuaVarDesc> fields;
	std::vector<LuaGroup> groups;

	/// Calls fn(const LuaVarDesc&) for every var
	template<typename F>
	void forEach(F&& fn) const {
		for (const auto& f : fields) {
			fn(f);
		}
		for (const auto& g : groups) {
			for (const auto& f : g.fields) {
				fn(f);
			}
			for (const auto& s : g.subgroups) {
				for (const auto& f : s.fields) {
					fn(f);
				}
			}
		}
	}

	/// Finds a var by its path
	[[nodiscard]]
	auto find(std::string_view path) const -> const LuaVarDesc* {
		const LuaVarDesc* found = nullptr;
		forEach([&](const LuaVarDesc& d) {
			if (found == nullptr && d.path == path) {
				found = &d;
			}
		});
		return found;
	}
};

}
