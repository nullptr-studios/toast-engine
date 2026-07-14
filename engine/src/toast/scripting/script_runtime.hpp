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
#include <toast/export.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/scripting/node_proxy.hpp>
#include <toast/scripting/script_schema.hpp>
#include <toast/world/box.hpp>
#include <vector>

namespace toast {
class Node;
}

namespace scripting {

// One per script
class ScriptInstance {
public:
	ScriptInstance(lua_State* l, const assets::AssetHandle<assets::Script>& script, NodeProxy proxy);
	~ScriptInstance() = default;

	ScriptInstance(ScriptInstance&&) = default;
	auto operator=(ScriptInstance&&) -> ScriptInstance& = default;
	ScriptInstance(const ScriptInstance&) = delete;
	auto operator=(const ScriptInstance&) -> ScriptInstance& = delete;

	/// Calls the named lifecycle function
	void call(std::string_view fn_name) noexcept;

	/// rawget self[name]; if a function, pcall(self, forwarded_args...).
	void callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept;

	/// Fan-out call with args provided as std::any values
	void callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept;

	/// Writes `value` to the instance variable named `name`
	/// @return true if the variable exists
	auto setVar(std::string_view name, const std::any& value) noexcept -> bool;

	/// Reads the instance variable named `name`
	[[nodiscard]]
	auto getVar(std::string_view name) const noexcept -> std::any;

	/// Reads a variable through its schema path
	[[nodiscard]]
	auto getVarByPath(std::string_view path) const noexcept -> std::any;

	/// Writes a variable through its schema path
	/// @return true if the variable exists
	auto setVarByPath(std::string_view path, const std::any& value) noexcept -> bool;

	/// Returns true if the self table has a callable field with this name
	[[nodiscard]]
	auto hasFunction(std::string_view fn_name) const noexcept -> bool;

	[[nodiscard]]
	auto schema() const noexcept -> const ScriptSchema& {
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
	ScriptSchema m_schema;
	toast::TickFunctionList m_tick_mask = toast::TickFunctionList::none;

	void installMetatable() noexcept;
	void extractSchema(std::string_view src) noexcept;
	void snapshotTickMask() noexcept;

	/// Pushes the value at `path` onto the Lua stack
	[[nodiscard]]
	auto pushByPath(std::string_view path) const noexcept -> bool;
};

// One per node
class TOAST_API ScriptRuntime {
public:
	ScriptRuntime(toast::Box<toast::Node> node, const std::vector<assets::AssetHandle<assets::Script>>& scripts);
	~ScriptRuntime() = default;

	ScriptRuntime(const ScriptRuntime&) = delete;
	auto operator=(const ScriptRuntime&) -> ScriptRuntime& = delete;

	/// Dispatch a TickFunctionList phase
	void call(toast::TickFunctionList phase) noexcept;

	/// Call a named function on all instances
	void call(std::string_view fn_name) noexcept;

	void callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept;

	/// Ccall with args provided as std::any values
	void callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept;

	/// Writes `value` to the variable named `name`
	void setVar(std::string_view name, const std::any& value) noexcept;

	/// Reads the variable named `name`
	[[nodiscard]]
	auto getVar(std::string_view name) const noexcept -> std::any;

	/// Number of attached script instances
	[[nodiscard]]
	auto instanceCount() const noexcept -> size_t {
		return m_instances.size();
	}

	/// Schema of one instance
	[[nodiscard]]
	auto instanceSchema(size_t index) const noexcept -> const ScriptSchema*;

	/// Script asset path of one instance
	[[nodiscard]]
	auto instanceScript(size_t index) const noexcept -> std::string_view;

	/// Reads a variable of one instance through its schema path
	[[nodiscard]]
	auto getVarByPath(size_t index, std::string_view path) const noexcept -> std::any;

	/// Writes a variable of one instance through its schema path
	auto setVarByPath(size_t index, std::string_view path, const std::any& value) noexcept -> bool;

	/// Changes whenever the runtime is rebuilt
	[[nodiscard]]
	auto schemaVersion() const noexcept -> uint32_t {
		return m_schema_version;
	}

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
	uint32_t m_schema_version = 0;
};

}
