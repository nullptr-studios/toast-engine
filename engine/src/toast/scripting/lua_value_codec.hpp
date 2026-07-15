/**
 * @file lua_value_codec.hpp
 * @author Xein
 * @date 14 Jul 2026
 * @brief Converts exported Lua script variable values to/from the engine's text encoding
 */

#pragma once

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <toast/scripting/script_schema.hpp>
#include <toast/world/box.hpp>

namespace toast {
class Node;
}

namespace scripting {

/// Resolves a node UID to a node reference
using NodeResolver = std::function<toast::Box<toast::Node>(std::string_view)>;

/// Encodes a Lua variable's current value as the engine's text encoding
auto stringifyLuaValue(const LuaVarDesc& desc, const std::any& value) -> std::string;

/// Decodes the engine's text encoding back into a value
auto parseLuaValue(const LuaVarDesc& desc, std::string_view text, const NodeResolver& find_node) -> std::any;

}
