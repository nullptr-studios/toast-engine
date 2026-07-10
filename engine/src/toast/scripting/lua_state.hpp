/**
 * @file lua_state.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

struct lua_State;

namespace scripting {

class LuaState {
public:
	static std::unique_ptr<LuaState> create() noexcept;
	static LuaState& get() noexcept;

	bool runString(std::string_view lua_code) noexcept;
	~LuaState() noexcept;

private:
	static inline LuaState* instance = nullptr;
	lua_State* m_state = nullptr;

	LuaState();

	void registerApi() noexcept;
};

}
