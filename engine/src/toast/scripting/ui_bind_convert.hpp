/**
 * @file ui_bind_convert.hpp
 * @author Xein
 * @date 22 Nov 2026
 */

#pragma once
#include <expected>
#include <string>
#include <toast/export.hpp>
#include <toast/ui/bind_value.hpp>

struct lua_State;

namespace scripting {

enum class BindKindHint : uint8_t {
	any,
	array,
	object
};

[[nodiscard]]
auto TOAST_API luaToBindValue(lua_State* l, int index, BindKindHint hint) -> std::expected<ui::BindValue, std::string>;

void TOAST_API pushBindValue(lua_State* l, const ui::BindValue& value);

}
