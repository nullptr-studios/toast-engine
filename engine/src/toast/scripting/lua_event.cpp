#include "lua_event.hpp"

#include "lua_state.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"
#include "script_runtime.hpp"

#include <atomic>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iterator>
#include <mutex>
#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include <unordered_map>

namespace scripting {

namespace _detail {
namespace {

template<typename Read>
auto withField(lua_State* state, int table, std::string_view field, Read&& read) -> bool {
	lua_getfield(state, table, std::string(field).c_str());
	const bool ok = read(lua_gettop(state));
	lua_pop(state, 1);
	return ok;
}

template<typename T>
auto readUserdata(lua_State* state, int table, std::string_view field, T& out) -> bool {
	return withField(state, table, field, [&](int index) {
		auto result = luabridge::Stack<T>::get(state, index);
		if (!result) {
			return false;
		}
		out = *result;
		return true;
	});
}

template<typename T>
void pushUserdata(lua_State* state, const T& value) {
	if (!luabridge::Stack<T>::push(state, value)) {
		lua_pushnil(state);
	}
}

}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, bool& out) -> bool {
	return withField(state, table, field, [&](int index) {
		if (!lua_isboolean(state, index)) {
			return false;
		}
		out = lua_toboolean(state, index) != 0;
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, int64_t& out) -> bool {
	return withField(state, table, field, [&](int index) {
		if (!lua_isinteger(state, index)) {
			return false;
		}
		out = static_cast<int64_t>(lua_tointeger(state, index));
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, double& out) -> bool {
	return withField(state, table, field, [&](int index) {
		if (!lua_isnumber(state, index)) {
			return false;
		}
		out = static_cast<double>(lua_tonumber(state, index));
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, std::string& out) -> bool {
	return withField(state, table, field, [&](int index) {
		if (lua_type(state, index) != LUA_TSTRING) {
			return false;
		}
		size_t length = 0;
		const char* text = lua_tolstring(state, index, &length);
		out.assign(text, length);
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, toast::UID& out) -> bool {
	return withField(state, table, field, [&](int index) {
		if (!lua_isinteger(state, index)) {
			return false;
		}
		out = toast::UID(static_cast<uint64_t>(lua_tointeger(state, index)));
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, toast::Box<toast::Node>& out) -> bool {
	return withField(state, table, field, [&](int index) {
		// nil is a valid "no node"
		if (lua_isnil(state, index)) {
			out = {};
			return true;
		}
		auto result = luabridge::Stack<NodeProxy>::get(state, index);
		if (!result) {
			return false;
		}
		out = (*result).box();
		return true;
	});
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec2& out) -> bool {
	return readUserdata(state, table, field, out);
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec3& out) -> bool {
	return readUserdata(state, table, field, out);
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec4& out) -> bool {
	return readUserdata(state, table, field, out);
}

auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::quat& out) -> bool {
	return readUserdata(state, table, field, out);
}

void pushEventPrimitive(lua_State* state, bool value) {
	lua_pushboolean(state, value ? 1 : 0);
}

void pushEventPrimitive(lua_State* state, int64_t value) {
	lua_pushinteger(state, static_cast<lua_Integer>(value));
}

void pushEventPrimitive(lua_State* state, double value) {
	lua_pushnumber(state, static_cast<lua_Number>(value));
}

void pushEventPrimitive(lua_State* state, std::string_view value) {
	lua_pushlstring(state, value.data(), value.size());
}

void pushEventPrimitive(lua_State* state, toast::UID value) {
	lua_pushinteger(state, static_cast<lua_Integer>(value.data()));
}

void pushEventPrimitive(lua_State* state, const toast::Box<toast::Node>& value) {
	if (!value.exists()) {
		lua_pushnil(state);
		return;
	}
	pushUserdata(state, NodeProxy(value));
}

void pushEventPrimitive(lua_State* state, const glm::vec2& value) {
	pushUserdata(state, value);
}

void pushEventPrimitive(lua_State* state, const glm::vec3& value) {
	pushUserdata(state, value);
}

void pushEventPrimitive(lua_State* state, const glm::vec4& value) {
	pushUserdata(state, value);
}

void pushEventPrimitive(lua_State* state, const glm::quat& value) {
	pushUserdata(state, value);
}

void pushEventTable(lua_State* state, int field_count) {
	lua_createtable(state, 0, field_count);
}

void setEventTableField(lua_State* state, std::string_view field) {
	lua_setfield(state, -2, std::string(field).c_str());
}

}

namespace {
std::mutex g_registry_mutex;
std::unordered_map<std::string, std::shared_ptr<LuaEventBinding>> g_bindings;

struct Subscription {
	std::atomic<bool> live {true};
	void* runtime = nullptr;
	toast::Box<toast::Node> node;
	size_t state_index = 0;
	std::string event_name;
	std::string listener_name;
	std::string method;
	std::unique_ptr<luabridge::LuaRef> function;
};

std::mutex g_subscription_mutex;
std::unordered_map<void*, std::vector<std::shared_ptr<Subscription>>> g_subscriptions;
std::atomic<uint64_t> g_next_subscription {0};

auto invoke(const std::weak_ptr<Subscription>& weak, const LuaEventBinding::Pusher& push) -> bool {
	auto sub = weak.lock();
	if (!sub || !sub->live.load() || !sub->node.exists() || sub->node->scriptRuntime() != sub->runtime) {
		return false;
	}
	auto guard = LuaState::get().lock(sub->state_index);
	if (!guard || !sub->live.load() || !sub->node.exists() || sub->node->scriptRuntime() != sub->runtime) {
		return false;
	}
	lua_State* state = guard.state();
	if (sub->function) {
		sub->function->push(state);
		push(state);
		if (pcallTraceback(state, 1, 1) != LUA_OK) {
			TOAST_ERROR("Lua", "Error in event callback: {}", lua_tostring(state, -1));
			lua_pop(state, 1);
			return false;
		}
		const bool consumed = lua_isboolean(state, -1) && lua_toboolean(state, -1) != 0;
		lua_pop(state, 1);
		return consumed;
	}
	push(state);
	const int event_index = lua_gettop(state);
	auto* runtime = static_cast<ScriptRuntime*>(sub->runtime);
	const bool consumed = runtime->callEventMethod(sub->method, state, event_index);
	lua_pop(state, 1);
	return consumed;
}

void eraseSubscriptions(void* runtime, std::string_view event_name, const std::string* method) {
	std::vector<std::shared_ptr<Subscription>> removed;
	{
		std::scoped_lock lock(g_subscription_mutex);
		auto found = g_subscriptions.find(runtime);
		if (found == g_subscriptions.end()) {
			return;
		}
		auto& subscriptions = found->second;
		for (auto it = subscriptions.begin(); it != subscriptions.end();) {
			const bool matches = (*it)->event_name == event_name && (!method || (*it)->method == *method);
			if (matches) {
				removed.push_back(*it);
				it = subscriptions.erase(it);
			} else {
				++it;
			}
		}
		if (subscriptions.empty()) {
			g_subscriptions.erase(found);
		}
	}
	for (auto& sub : removed) {
		sub->live.store(false);
		if (sub->node.exists()) {
			if (auto binding = LuaEventRegistry::find(sub->event_name)) {
				binding->unsubscribe(sub->node->listener(), sub->listener_name);
			}
		}
	}
}
}

void LuaEventRegistry::registerBinding(std::shared_ptr<LuaEventBinding> binding) {
	std::scoped_lock lock(g_registry_mutex);
	g_bindings.insert_or_assign(binding->name, std::move(binding));
}

auto LuaEventRegistry::find(std::string_view name) -> std::shared_ptr<const LuaEventBinding> {
	std::scoped_lock lock(g_registry_mutex);
	auto found = g_bindings.find(std::string(name));
	return found == g_bindings.end() ? nullptr : found->second;
}

auto LuaEventRegistry::descriptors() -> std::vector<LuaEventDescriptor> {
	std::scoped_lock lock(g_registry_mutex);
	std::vector<LuaEventDescriptor> result;
	result.reserve(g_bindings.size());
	for (const auto& [name, _] : g_bindings) {
		result.push_back({name});
	}
	return result;
}

void LuaEventRegistry::installDescriptors(lua_State* state) {
	lua_newtable(state);
	for (const auto& descriptor : descriptors()) {
		if (auto pushed = luabridge::Stack<LuaEventDescriptor>::push(state, descriptor); pushed) {
			lua_setfield(state, -2, descriptor.name.c_str());
		}
	}
	lua_setglobal(state, "Events");
}

void ListenerProxy::subscribe(const LuaEventDescriptor& descriptor, const luabridge::LuaRef& callback, lua_State* state) {
	if (!m_node.exists() || !m_node->scriptRuntime()) {
		luaL_error(state, "listener.subscribe: node has no live script runtime");
		return;
	}
	auto binding = LuaEventRegistry::find(descriptor.name);
	if (!binding) {
		luaL_error(state, "listener.subscribe: unknown event descriptor '%s'", descriptor.name.c_str());
		return;
	}
	if (!callback.isString() && !callback.isFunction()) {
		luaL_error(state, "listener.subscribe: callback must be a method name or function");
		return;
	}
	auto* runtime = m_node->scriptRuntime();
	auto sub = std::make_shared<Subscription>();
	sub->runtime = runtime;
	sub->node = m_node;
	sub->state_index = runtime->stateIndex();
	sub->event_name = descriptor.name;
	sub->listener_name = "__lua_event_" + std::to_string(++g_next_subscription);
	if (callback.isString()) {
		sub->method = callback.tostring();
	} else {
		sub->function = std::make_unique<luabridge::LuaRef>(callback);
	}
	{
		std::scoped_lock lock(g_subscription_mutex);
		g_subscriptions[runtime].push_back(sub);
	}
	binding->subscribe(m_node->listener(), sub->listener_name, [weak = std::weak_ptr(sub)](const LuaEventBinding::Pusher& push) {
		return invoke(weak, push);
	});
}

void ListenerProxy::unsubscribe(const LuaEventDescriptor& descriptor) {
	if (m_node.exists() && m_node->scriptRuntime()) {
		eraseSubscriptions(m_node->scriptRuntime(), descriptor.name, nullptr);
	}
}

void ListenerProxy::unsubscribe(const LuaEventDescriptor& descriptor, const std::string& method) {
	if (m_node.exists() && m_node->scriptRuntime()) {
		eraseSubscriptions(m_node->scriptRuntime(), descriptor.name, &method);
	}
}

void clearLuaEventSubscriptions(void* runtime) noexcept {
	std::vector<std::shared_ptr<Subscription>> removed;
	{
		std::scoped_lock lock(g_subscription_mutex);
		auto found = g_subscriptions.find(runtime);
		if (found == g_subscriptions.end()) {
			return;
		}
		removed = std::move(found->second);
		g_subscriptions.erase(found);
	}
	for (auto& sub : removed) {
		sub->live.store(false);
		if (sub->node.exists()) {
			if (auto binding = LuaEventRegistry::find(sub->event_name)) {
				binding->unsubscribe(sub->node->listener(), sub->listener_name);
			}
		}
	}
}

void clearAllLuaEventSubscriptions() noexcept {
	std::vector<std::shared_ptr<Subscription>> removed;
	{
		std::scoped_lock lock(g_subscription_mutex);
		for (auto& [_, subscriptions] : g_subscriptions) {
			removed.insert(removed.end(), std::make_move_iterator(subscriptions.begin()), std::make_move_iterator(subscriptions.end()));
		}
		g_subscriptions.clear();
	}
	for (auto& sub : removed) {
		auto guard = LuaState::get().lock(sub->state_index);
		sub->live.store(false);
		if (sub->node.exists()) {
			if (auto binding = LuaEventRegistry::find(sub->event_name)) {
				binding->unsubscribe(sub->node->listener(), sub->listener_name);
			}
		}
		sub->function.reset();
	}
}
}
