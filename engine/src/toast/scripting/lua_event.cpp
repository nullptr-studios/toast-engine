#include "lua_event.hpp"

#include "asset_proxy.hpp"
#include "lua_callback.hpp"
#include "lua_state.hpp"
#include "lua_util.hpp"
#include "node_proxy.hpp"
#include "script_runtime.hpp"

#include <atomic>
#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <iterator>
#include <mutex>
#include <toast/input/action.hpp>
#include <toast/log.hpp>
#include <toast/physics/contact_events.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/world/node.hpp>
#include <unordered_map>

namespace scripting {

namespace _detail {
namespace {

template<typename T>
auto readUserdata(lua_State* state, int index, T& out) -> bool {
	auto result = luabridge::Stack<T>::get(state, index);
	if (!result) {
		return false;
	}
	out = *result;
	return true;
}

template<typename T>
void pushUserdata(lua_State* state, const T& value) {
	if (!luabridge::Stack<T>::push(state, value)) {
		lua_pushnil(state);
	}
}

// glm::ivec support
// Casted to float Vec on lua since ints more or less doesnt exist
template<typename Float, typename Int>
auto readIntVector(lua_State* state, int index, Int& out) -> bool {
	Float value {};
	if (!readUserdata(state, index, value)) {
		return false;
	}
	out = Int(glm::round(value));
	return true;
}

auto packId(uint32_t slot, uint32_t generation) -> int64_t {
	return static_cast<int64_t>((static_cast<uint64_t>(generation) << 32U) | slot);
}

void pushBodyShape(lua_State* state, const physics::BodyShapeKey& key) {
	lua_createtable(state, 0, 3);
	pushEventPrimitive(state, physics::Simulator::nodeFor(key.body));
	lua_setfield(state, -2, "node");
	pushEventPrimitive(state, packId(key.body.slot, key.body.generation));
	lua_setfield(state, -2, "body");
	pushEventPrimitive(state, packId(key.shape.slot, key.shape.generation));
	lua_setfield(state, -2, "shape");
}

}

auto readEventPrimitive(lua_State* state, int index, bool& out) -> bool {
	if (!lua_isboolean(state, index)) {
		return false;
	}
	out = lua_toboolean(state, index) != 0;
	return true;
}

auto readEventPrimitive(lua_State* state, int index, int64_t& out) -> bool {
	if (!lua_isinteger(state, index)) {
		return false;
	}
	out = static_cast<int64_t>(lua_tointeger(state, index));
	return true;
}

auto readEventPrimitive(lua_State* state, int index, double& out) -> bool {
	if (!lua_isnumber(state, index)) {
		return false;
	}
	out = static_cast<double>(lua_tonumber(state, index));
	return true;
}

auto readEventPrimitive(lua_State* state, int index, std::string& out) -> bool {
	if (lua_type(state, index) != LUA_TSTRING) {
		return false;
	}
	size_t length = 0;
	const char* text = lua_tolstring(state, index, &length);
	out.assign(text, length);
	return true;
}

auto readEventPrimitive(lua_State* state, int index, toast::UID& out) -> bool {
	// Integers are accepted so node:uid() or asset:uid() can be passed
	if (lua_isinteger(state, index)) {
		out = toast::UID(static_cast<uint64_t>(lua_tointeger(state, index)));
		return true;
	}
	std::string text;
	if (!readEventPrimitive(state, index, text) || (!text.empty() && text.size() != 11)) {
		return false;
	}
	out = toast::UID(toast::UID::fromString(text));
	return true;
}

auto readEventPrimitive(lua_State* state, int index, toast::Box<toast::Node>& out) -> bool {
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
}

auto readEventPrimitive(lua_State* state, int index, toast::Node*& out) -> bool {
	toast::Box<toast::Node> box;
	if (!readEventPrimitive(state, index, box)) {
		return false;
	}
	out = box.exists() ? &*box : nullptr;
	return true;
}

auto readEventPrimitive(lua_State* state, int index, glm::vec2& out) -> bool {
	return readUserdata(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::vec3& out) -> bool {
	return readUserdata(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::vec4& out) -> bool {
	return readUserdata(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::ivec2& out) -> bool {
	return readIntVector<glm::vec2>(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::ivec3& out) -> bool {
	return readIntVector<glm::vec3>(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::ivec4& out) -> bool {
	return readIntVector<glm::vec4>(state, index, out);
}

auto readEventPrimitive(lua_State* state, int index, glm::quat& out) -> bool {
	return readUserdata(state, index, out);
}

auto readEventHandle(lua_State* state, int index, std::string_view type, assets::HandleBase& out) -> bool {
	// nil is a valid "no asset"
	if (lua_isnil(state, index)) {
		out = {};
		return true;
	}
	auto result = luabridge::Stack<AssetProxy>::get(state, index);
	if (!result) {
		return false;
	}
	if (const std::string error = (*result).checkType(type); !error.empty()) {
		TOAST_WARN("Lua", "event.send: {}", error);
		return false;
	}
	out = (*result).handle();
	return true;
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
	pushEventPrimitive(state, std::string_view(value.get()));
}

void pushEventPrimitive(lua_State* state, const toast::Box<toast::Node>& value) {
	if (!value.exists()) {
		lua_pushnil(state);
		return;
	}
	pushUserdata(state, NodeProxy(value));
}

void pushEventPrimitive(lua_State* state, const toast::Node* value) {
	if (value == nullptr) {
		lua_pushnil(state);
		return;
	}
	pushEventPrimitive(state, toast::Box<toast::Node>(value));
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

void pushEventPrimitive(lua_State* state, const glm::ivec2& value) {
	pushUserdata(state, glm::vec2(value));
}

void pushEventPrimitive(lua_State* state, const glm::ivec3& value) {
	pushUserdata(state, glm::vec3(value));
}

void pushEventPrimitive(lua_State* state, const glm::ivec4& value) {
	pushUserdata(state, glm::vec4(value));
}

void pushEventPrimitive(lua_State* state, const glm::quat& value) {
	pushUserdata(state, value);
}

void pushEventPrimitive(lua_State* state, const assets::HandleBase& value) {
	pushUserdata(state, AssetProxy(value));
}

void pushEventPrimitive(lua_State* state, const input::Action& value) {
	pushUserdata(state, value);
}

void pushEventPrimitive(lua_State* state, const physics::BroadPhasePair& value) {
	lua_createtable(state, 0, 2);
	pushBodyShape(state, value.a);
	lua_setfield(state, -2, "a");
	pushBodyShape(state, value.b);
	lua_setfield(state, -2, "b");
}

void pushEventPrimitive(lua_State* state, const event::ContactEventData& value) {
	lua_createtable(state, 0, 3);
	pushEventPrimitive(state, value.pair);
	lua_setfield(state, -2, "pair");
	pushEventPrimitive(state, value.normal);
	lua_setfield(state, -2, "normal");
	lua_createtable(state, value.contact_count, 0);
	for (uint8_t i = 0; i < value.contact_count; ++i) {
		const physics::ContactPoint& point = value.contacts[i];
		lua_createtable(state, 0, 2);
		pushEventPrimitive(state, point.position);
		lua_setfield(state, -2, "position");
		pushEventPrimitive(state, static_cast<double>(point.penetration));
		lua_setfield(state, -2, "penetration");
		lua_rawseti(state, -2, i + 1);
	}
	lua_setfield(state, -2, "contacts");
}

void pushEventTable(lua_State* state, int field_count) {
	lua_createtable(state, 0, field_count);
}

void setEventTableField(lua_State* state, std::string_view field) {
	lua_setfield(state, -2, std::string(field).c_str());
}

auto pushEventField(lua_State* state, int table, std::string_view field) -> int {
	lua_getfield(state, table, std::string(field).c_str());
	return lua_gettop(state);
}

auto pushEventElement(lua_State* state, int table, int64_t i) -> int {
	lua_rawgeti(state, table, static_cast<lua_Integer>(i));
	return lua_gettop(state);
}

void popEventValue(lua_State* state) {
	lua_pop(state, 1);
}

auto eventArrayLength(lua_State* state, int index) -> int64_t {
	if (!lua_istable(state, index)) {
		return -1;
	}
	return static_cast<int64_t>(lua_rawlen(state, index));
}

void pushEventArray(lua_State* state, size_t size) {
	lua_createtable(state, static_cast<int>(size), 0);
}

void setEventArrayElement(lua_State* state, int64_t i) {
	lua_rawseti(state, -2, static_cast<lua_Integer>(i));
}

}

namespace {
std::mutex g_registry_mutex;
// Leaked on purpose don't even ask
auto& g_bindings = *new std::unordered_map<std::string, std::shared_ptr<LuaEventBinding>>();

struct Subscription {
	std::atomic<bool> live {true};
	void* runtime = nullptr;                ///< key of the runtime that owns the listener node
	std::shared_ptr<RuntimeToken> token;    ///< dies with that runtime
	toast::Box<toast::Node> node;
	size_t state_index = 0;                 ///< interpreter of the runtime, where named methods live
	std::string event_name;
	std::string listener_name;
	std::string method;
	LuaCallback function;    ///< closures carry their own interpreter, released through its retire queue
};

std::mutex g_subscription_mutex;
std::unordered_map<void*, std::vector<std::shared_ptr<Subscription>>> g_subscriptions;
std::atomic<uint64_t> g_next_subscription {0};

auto subscriptionUsable(const Subscription& sub) -> bool {
	return sub.live.load() && sub.node.exists() && (sub.token == nullptr || sub.token->alive.load(std::memory_order_acquire));
}

auto invoke(const std::weak_ptr<Subscription>& weak, const LuaEventBinding::Pusher& push) -> bool {
	const auto sub = weak.lock();
	if (!sub || !subscriptionUsable(*sub)) {
		return false;
	}
	if (sub->function) {
		// The closure owns its interpreter; the event only exists for the duration of this call
		return sub->function.invokeWithArgument(push).value_or(false);
	}

	auto guard = LuaState::get().lock(sub->state_index);
	if (!guard || !subscriptionUsable(*sub)) {
		return false;
	}
	ScriptRuntime* runtime = sub->node->scriptRuntime();
	if (runtime == nullptr || runtime->token() != sub->token) {
		return false;
	}
	lua_State* state = guard.state();
	push(state);
	const int event_index = lua_gettop(state);
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
	sub->token = runtime->token();
	sub->node = m_node;
	sub->state_index = runtime->stateIndex();
	sub->event_name = descriptor.name;
	sub->listener_name = "__lua_event_" + std::to_string(++g_next_subscription);
	if (callback.isString()) {
		sub->method = callback.tostring();
	} else {
		// A closure lives on the interpreter of the script that wrote it, which is not necessarily the node's
		sub->function = LuaCallback::capture(callback, "event callback");
		if (!sub->function) {
			luaL_error(state, "listener.subscribe: the callback does not belong to a script interpreter");
			return;
		}
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
	// Dropping the last reference releases the Lua function through the interpreter, whichever thread that is
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
		sub->live.store(false);
		if (sub->node.exists()) {
			if (auto binding = LuaEventRegistry::find(sub->event_name)) {
				binding->unsubscribe(sub->node->listener(), sub->listener_name);
			}
		}
		sub->function = LuaCallback();
	}
}
}
