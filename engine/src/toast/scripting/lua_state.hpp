/**
 * @file lua_state.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Holds the Lua interpreter and enviroment table
 */

#pragma once

struct lua_State;

namespace scripting {

class LuaState {
public:
	static auto create() noexcept -> std::unique_ptr<LuaState>;
	static auto get() noexcept -> LuaState&;
	~LuaState() noexcept;

	auto runString(std::string_view lua_code) noexcept -> bool;

	/// Returns the raw Lua state; used by scripting subsystems
	[[nodiscard]]
	auto state() const noexcept -> lua_State* {
		return m_state;
	}

private:
	static inline LuaState* instance = nullptr;
	lua_State* m_state = nullptr;

	LuaState();

	void registerApi() noexcept;
};

}
