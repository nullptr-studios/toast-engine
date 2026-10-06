/**
 * @file script_runtime.hpp
 * @author Xein
 * @date 10 Jul 2026
 *
 * @brief Per-node Lua script execution environment
 *
 * Everything in here that touches Lua happens while the calling thread owns the interpreter the runtime lives on
 * Calls that arrive while it is busy on another thread are queued in ScriptDispatch
 */

#pragma once

#include <any>
#include <atomic>
#include <cstdint>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <toast/assets/script.hpp>
#include <toast/export.hpp>
#include <toast/reflect/reflect_node.hpp>
#include <toast/scripting/lua_signal.hpp>
#include <toast/scripting/node_proxy.hpp>
#include <toast/scripting/script_context.hpp>
#include <toast/scripting/script_schema.hpp>
#include <toast/world/box.hpp>
#include <unordered_map>
#include <vector>

namespace toast {
class Node;
}

namespace scripting {

/// Shared between a ScriptInstance and the closures installed in its self table; the closures never see a dangling instance
struct ScriptBinding {
	explicit ScriptBinding(NodeProxy node_proxy) : proxy(std::move(node_proxy)) { }

	NodeProxy proxy;
	std::atomic<bool> alive {true};    ///< false once the instance is destroyed
};

// One per script
class ScriptInstance {
public:
	ScriptInstance(
	    size_t vm_index, lua_State* l, const assets::Handle<assets::Script>& script, NodeProxy proxy,
	    std::shared_ptr<RuntimeToken> token
	);

	/// The owner of the interpreter has to run this, it releases registry references
	~ScriptInstance();

	ScriptInstance(ScriptInstance&&) = delete;
	auto operator=(ScriptInstance&&) -> ScriptInstance& = delete;
	ScriptInstance(const ScriptInstance&) = delete;
	auto operator=(const ScriptInstance&) -> ScriptInstance& = delete;

	/// Calls the named lifecycle function
	void call(std::string_view fn_name) noexcept;

	/// rawget self[name]; if a function, pcall(self, forwarded_args...)
	void callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept;

	/// Calls a lua method on the self table
	auto callEventMethod(std::string_view name, lua_State* l, int event_index) noexcept -> bool;

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

	/// Returns true if the self table has a callable field with this name. Needs the interpreter
	[[nodiscard]]
	auto hasFunction(std::string_view fn_name) const noexcept -> bool;

	/// True when the script defined a function with this name when it was loaded; touches no interpreter
	[[nodiscard]]
	auto knowsFunction(std::string_view fn_name) const noexcept -> bool {
		return m_function_names.contains(fn_name);
	}

	[[nodiscard]]
	auto luaSignals() const noexcept -> const std::unordered_map<std::string, LuaSignal>& {
		return m_lua_signals;
	}

	[[nodiscard]]
	auto luaSignals() noexcept -> std::unordered_map<std::string, LuaSignal>& {
		return m_lua_signals;
	}

	[[nodiscard]]
	auto schema() const noexcept -> const ScriptSchema& {
		return m_schema;
	}

	/// Cached at load, never touches the interpreter
	[[nodiscard]]
	auto isValid() const noexcept -> bool {
		return m_valid;
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
	size_t m_vm_index = 0;
	std::shared_ptr<ScriptBinding> m_binding;
	std::shared_ptr<RuntimeToken> m_token;
	std::unique_ptr<luabridge::LuaRef> m_self;
	bool m_valid = false;
	std::string m_name;
	ScriptSchema m_schema;
	std::unordered_map<std::string, LuaSignal> m_lua_signals;
	std::set<std::string, std::less<>>
	    m_function_names;    ///< every function the table had at load, underscore prefixed ones included
	toast::TickFunctionList m_tick_mask = toast::TickFunctionList::none;

	void installMetatable() noexcept;
	void extractSchema(std::string_view src) noexcept;
	void snapshotTickMask() noexcept;

	/// Pushes the value at `path` onto the Lua stack
	[[nodiscard]]
	auto pushByPath(std::string_view path) const noexcept -> bool;
};

/// The node a script self table belongs to; empty when the table is not the self table of a live script instance
[[nodiscard]]
auto nodeOfSelfTable(const luabridge::LuaRef& table) -> NodeProxy;

// One per node
class TOAST_API ScriptRuntime {
public:
	/**
	 * @param group Places the runtime on the interpreter of its script group, 0 spreads runtimes round robin
	 * @param placed An interpreter LuaState::assign() already picked for this runtime, which the runtime takes over and gives
	 *        back when it goes. It lets the caller know where the runtime will live before it is built
	 */
	ScriptRuntime(
	    toast::Box<toast::Node> node, const std::vector<assets::Handle<assets::Script>>& scripts, uint64_t group = 0,
	    std::optional<size_t> placed = std::nullopt
	);
	~ScriptRuntime();

	ScriptRuntime(const ScriptRuntime&) = delete;
	auto operator=(const ScriptRuntime&) -> ScriptRuntime& = delete;

	/// Dispatch a TickFunctionList phase
	void call(toast::TickFunctionList phase) noexcept;

	/// Call a named function on all instances
	void call(std::string_view fn_name) noexcept;

	/// True if any attached script instance exposes a callable field with this name
	[[nodiscard]]
	auto hasFunction(std::string_view fn_name) const noexcept -> bool;

	[[nodiscard]]
	auto functions() const noexcept -> std::vector<LuaFunctionDesc>;

	void callWithLuaStack(std::string_view name, lua_State* l, int args_base, int n_args) noexcept;
	auto callEventMethod(std::string_view name, lua_State* l, int event_index) noexcept -> bool;

	/// Ccall with args provided as std::any values
	void callWithAnyArgs(std::string_view name, std::span<const std::any> args) noexcept;

	/// Writes `value` to the variable named `name`
	void setVar(std::string_view name, const std::any& value) noexcept;

	/// Reads the variable named `name`
	[[nodiscard]]
	auto getVar(std::string_view name) const noexcept -> std::any;

	[[nodiscard]]
	auto luaSignals() const -> std::vector<std::string>;
	[[nodiscard]]
	auto luaSignalConnections(std::string_view name) const -> std::vector<signals::ConnectionInfo>;
	[[nodiscard]]
	auto luaSignalArgTypes(std::string_view name) const -> std::vector<std::string>;
	auto connectLuaSignal(std::string_view name, toast::Node& target, std::string_view function, bool forwards_args) -> bool;
	auto disconnectLuaSignal(std::string_view name, toast::Node& target, std::string_view function) -> bool;
	void clearLuaSignal(std::string_view name);

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

	/// Unique for every runtime that was ever built, so it also tells a rebuilt runtime from the one it replaced
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

	/// Flips to dead as the runtime is torn down. Everything that calls into the scripts later holds it
	[[nodiscard]]
	auto token() const noexcept -> const std::shared_ptr<RuntimeToken>& {
		return m_token;
	}

	/// True while a call into this runtime is on some thread's stack
	[[nodiscard]]
	auto executing() const noexcept -> bool {
		return m_active_calls.load(std::memory_order_acquire) != 0;
	}

	/**
	 * @brief Destroys a runtime that may still be executing
	 *
	 * A runtime replaced from inside one of its own calls would be freed under its own stack frames. It waits in a
	 * list instead, and drainRetired() destroys it once nothing runs in it any more
	 */
	static void retire(std::unique_ptr<ScriptRuntime> runtime) noexcept;

	/// Destroys retired runtimes that finished executing; call from the main thread when no script is running
	static void drainRetired() noexcept;

private:
	std::vector<std::unique_ptr<ScriptInstance>> m_instances;
	size_t m_state_index = 0;
	uint64_t m_group = 0;
	bool m_placed = false;    ///< LuaState::assign() handed out m_state_index, and unassign() has to give it back
	lua_State* m_lua = nullptr;
	toast::Box<toast::Node> m_node;
	std::shared_ptr<RuntimeToken> m_token;
	toast::TickFunctionList m_tick_mask = toast::TickFunctionList::none;
	uint32_t m_schema_version = 0;
	mutable std::atomic<int> m_active_calls {0};

	/// Queues `work` for delivery when the interpreter is busy; it only runs if this runtime is still the node's runtime
	template<typename F>
	void deferToDispatch(F&& work) const;
};

}
