#pragma once

#include "toast/assets/script.hpp"
#include "toast/scripting/lua_state.hpp"
#include "toast/world/world_test_access.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace toast::tests::scripting_tests {

/// One LuaState pool per test process; created lazily on first use
inline auto luaState() -> ::scripting::LuaState& {
	static auto state = ::scripting::LuaState::create();
	return *state;
}

/// Builds a Script asset handle from inline source; storage outlives the test
inline auto makeScript(std::string_view source, uint64_t uid = 1) -> assets::Handle<assets::Script> {
	static std::vector<std::unique_ptr<assets::Script>> storage;
	storage.push_back(std::make_unique<assets::Script>(std::vector<uint8_t>(source.begin(), source.end())));
	return {storage.back().get(), toast::UID {uid}, "test://script.lua"};
}

}
