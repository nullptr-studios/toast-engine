#include "lua_event.hpp"

#include "lua_state.hpp"
#include "lua_util.hpp"
#include "script_runtime.hpp"

#include <atomic>
#include <iterator>
#include <mutex>
#include <toast/log.hpp>
#include <toast/world/node.hpp>
#include <unordered_map>

namespace scripting {
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
