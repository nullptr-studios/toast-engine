/**
 * @file script_runtime.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Per-node Lua script execution environment
 */

#pragma once

#include <any>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <toast/assets/script.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/scripting/node_proxy.hpp>
#include <toast/world/box.hpp>
#include <vector>

namespace toast {
class Node;
}

namespace scripting {

// One per script
class ScriptInstance {
public:
	/// A non-function key present in the returned table at load time
	struct ExportEntry {
		std::string name;
		luabridge::LuaRef value;
	};

	ScriptInstance(lua_State* L, const assets::AssetHandle<assets::Script>& script, const NodeProxy& proxy);
	~ScriptInstance() = default;

	ScriptInstance(ScriptInstance&&) = default;
	ScriptInstance& operator=(ScriptInstance&&) = default;
	ScriptInstance(const ScriptInstance&) = delete;
	ScriptInstance& operator=(const ScriptInstance&) = delete;

	/// Calls the named lifecycle function
	void call(std::string_view fn_name) noexcept;

	/// rawget self[name]; if a function, pcall(self, forwarded_args...).
	void callWithLuaStack(std::string_view name, lua_State* L, int args_base, int n_args) noexcept;

	/// Fan-out call with args provided as std::any values
	void callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept;

	/// Writes `value` to the instance variable named `name`
	void setVar(std::string_view name, const std::any& value) noexcept;

	/// Reads the instance variable named `name`
	[[nodiscard]]
	auto getVar(std::string_view name) const noexcept -> std::any;

	/// Returns true if the self table has a callable field with this name
	[[nodiscard]]
	auto hasFunction(std::string_view fn_name) const noexcept -> bool;

	[[nodiscard]]
	auto schema() const noexcept -> const std::vector<ExportEntry>& {
		return m_schema;
	}

	[[nodiscard]]
	auto isValid() const noexcept -> bool {
		return m_self && !m_self->isNil();
	}

	/// Tick phases this script defines
	[[nodiscard]]
	auto tickMask() const noexcept -> toast::TickFunctionList {
		return m_tick_mask;
	}

	/// Asset path of the script
	[[nodiscard]]
	auto name() const noexcept -> const std::string& {
		return m_name;
	}

private:
	lua_State* m_state = nullptr;
	std::unique_ptr<luabridge::LuaRef> m_self;
	NodeProxy m_proxy;
	std::string m_name;
	std::vector<ExportEntry> m_schema;
	toast::TickFunctionList m_tick_mask = toast::TickFunctionList::none;

	void installMetatable() noexcept;
	void snapshotSchema() noexcept;
	void snapshotTickMask() noexcept;
};

// One per node
class ScriptRuntime {
public:
	ScriptRuntime(toast::Box<toast::Node> node, const std::vector<assets::AssetHandle<assets::Script>>& scripts);
	~ScriptRuntime() = default;

	ScriptRuntime(const ScriptRuntime&) = delete;
	ScriptRuntime& operator=(const ScriptRuntime&) = delete;

	/// Dispatch a TickFunctionList phase
	void call(toast::TickFunctionList phase) noexcept;

	/// Call a named function on all instances
	void call(std::string_view fn_name) noexcept;

	void callWithLuaStack(std::string_view name, lua_State* L, int args_base, int n_args) noexcept;

	/// Ccall with args provided as std::any values
	void callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept;

	/// Writes `value` to the variable named `name`
	void setVar(std::string_view name, const std::any& value) noexcept;

	/// Reads the variable named `name`
	[[nodiscard]]
	auto getVar(std::string_view name) const noexcept -> std::any;

	/// True if any instance defines a function matching the given phases
	[[nodiscard]]
	auto hasTick(toast::TickFunctionList mask = toast::TickFunctionList::tick_mask) const noexcept -> bool {
		return (m_tick_mask & mask) != toast::TickFunctionList::none;
	}

	/// Index of the pooled Lua state this runtime is bound to
	[[nodiscard]]
	auto stateIndex() const noexcept -> size_t {
		return m_state_index;
	}

private:
	std::vector<std::unique_ptr<ScriptInstance>> m_instances;
	size_t m_state_index = 0;
	lua_State* m_lua = nullptr;
	toast::TickFunctionList m_tick_mask = toast::TickFunctionList::none;
};

}
