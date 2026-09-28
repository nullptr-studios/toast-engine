/**
 * @file lua_event.hpp
 * @author Xein
 * @date 25 Sep 2026
 * @brief Lua event registration and binding for Toast events
 */

#pragma once

#include <concepts>
#include <functional>
#include <glm/fwd.hpp>
#include <lua.hpp>
#include <luabridge3/LuaBridge/LuaBridge.h>
#include <memory>
#include <string>
#include <string_view>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <toast/world/box.hpp>
#include <tuple>
#include <type_traits>
#include <utility>

namespace toast {
class Node;
}

namespace scripting {

struct LuaEventDescriptor {
	std::string name;
};

struct LuaEventBinding {
	using Pusher = std::function<void(lua_State*)>;
	using Callback = std::function<bool(const Pusher&)>;

	std::string name;
	std::function<bool(lua_State*, int, std::string&)> send;
	std::function<void(event::Listener&, std::string, Callback)> subscribe;
	std::function<void(event::Listener&, std::string_view)> unsubscribe;
};

class TOAST_API LuaEventRegistry {
public:
	static void registerBinding(std::shared_ptr<LuaEventBinding> binding);
	[[nodiscard]]
	static auto find(std::string_view name) -> std::shared_ptr<const LuaEventBinding>;
	[[nodiscard]]
	static auto descriptors() -> std::vector<LuaEventDescriptor>;
	static void installDescriptors(lua_State* state);
};

template<typename Owner, typename Member>
struct LuaEventField {
	std::string_view name;
	Member Owner::* member;
};

template<typename Owner, typename Member>
constexpr auto luaEventField(std::string_view name, Member Owner::* member) -> LuaEventField<Owner, Member> {
	return {name, member};
}

namespace _detail {
template<typename T>
auto readEventValue(lua_State* state, int table, std::string_view field, T& out, std::string& error) -> bool {
	lua_getfield(state, table, std::string(field).c_str());
	const int value = lua_gettop(state);
	bool ok = true;
	if constexpr (std::same_as<T, bool>) {
		ok = lua_isboolean(state, value) != 0;
		if (ok) {
			out = lua_toboolean(state, value) != 0;
		}
	} else if constexpr (std::same_as<T, toast::UID>) {
		ok = lua_isinteger(state, value) != 0;
		if (ok) {
			out = toast::UID(static_cast<uint64_t>(lua_tointeger(state, value)));
		}
	} else if constexpr (std::integral<T>) {
		ok = lua_isinteger(state, value) != 0;
		if (ok) {
			out = static_cast<T>(lua_tointeger(state, value));
		}
	} else if constexpr (std::floating_point<T>) {
		ok = lua_isnumber(state, value) != 0;
		if (ok) {
			out = static_cast<T>(lua_tonumber(state, value));
		}
	} else if constexpr (std::same_as<T, std::string>) {
		ok = lua_type(state, value) == LUA_TSTRING;
		if (ok) {
			out = lua_tostring(state, value);
		}
	} else {
		auto result = luabridge::Stack<T>::get(state, value);
		ok = static_cast<bool>(result);
		if (ok) {
			out = std::move(*result);
		}
	}
	lua_pop(state, 1);
	if (!ok) {
		error = "invalid or missing field '" + std::string(field) + "'";
	}
	return ok;
}

template<typename T>
void pushEventValue(lua_State* state, const T& value) {
	if constexpr (std::same_as<T, bool>) {
		lua_pushboolean(state, value ? 1 : 0);
	} else if constexpr (std::same_as<T, toast::UID>) {
		lua_pushinteger(state, static_cast<lua_Integer>(value.data()));
	} else if constexpr (std::integral<T>) {
		lua_pushinteger(state, static_cast<lua_Integer>(value));
	} else if constexpr (std::floating_point<T>) {
		lua_pushnumber(state, static_cast<lua_Number>(value));
	} else if constexpr (std::same_as<T, std::string>) {
		lua_pushlstring(state, value.data(), value.size());
	} else if (auto result = luabridge::Stack<T>::push(state, value); !result) {
		lua_pushnil(state);
	}
}

template<typename Event, typename... Fields, size_t... I>
auto sendEvent(lua_State* state, int payload, std::string& error, const std::tuple<Fields...>& fields, std::index_sequence<I...>)
    -> bool {
	std::tuple<std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Event>().*std::declval<Fields>().member)>>...> values;
	const bool valid = (readEventValue(state, payload, std::get<I>(fields).name, std::get<I>(values), error) && ...);
	if (!valid) {
		return false;
	}
	std::apply([](auto&&... args) { event::send<Event>(std::move(args)...); }, values);
	return true;
}

template<typename Event, typename... Fields, size_t... I>
void pushEvent(lua_State* state, const Event& value, const std::tuple<Fields...>& fields, std::index_sequence<I...>) {
	lua_createtable(state, 0, sizeof...(Fields));
	((pushEventValue(state, value.*(std::get<I>(fields).member)),
	  lua_setfield(state, -2, std::string(std::get<I>(fields).name).c_str())),
	 ...);
}
}

template<typename Event, typename... Fields>
void registerLuaEvent(std::string_view name, Fields... fields) {
	using Values = std::tuple<std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Event>().*fields.member)>>...>;
	static_assert(
	    []<typename... T>(std::tuple<T...>*) { return std::is_constructible_v<Event, T...>; }(static_cast<Values*>(nullptr)),
	    "Lua event fields must match a callable event constructor"
	);
	auto metadata = std::make_shared<std::tuple<Fields...>>(fields...);
	auto binding = std::make_shared<LuaEventBinding>();
	binding->name = name;
	binding->send = [metadata](lua_State* state, int payload, std::string& error) {
		return _detail::sendEvent<Event>(state, payload, error, *metadata, std::index_sequence_for<Fields...> {});
	};
	binding->subscribe = [metadata](event::Listener& listener, std::string subscription_name, LuaEventBinding::Callback callback) {
		listener.subscribe<Event>(std::move(subscription_name), [metadata, callback = std::move(callback)](Event& value) {
			return callback([metadata, &value](lua_State* state) {
				_detail::pushEvent(state, value, *metadata, std::index_sequence_for<Fields...> {});
			});
		});
	};
	binding->unsubscribe = [](event::Listener& listener, std::string_view subscription_name) {
		listener.unsubscribe<Event>(subscription_name);
	};
	LuaEventRegistry::registerBinding(std::move(binding));
}

class ListenerProxy {
public:
	ListenerProxy() = default;

	explicit ListenerProxy(toast::Box<toast::Node> node) : m_node(std::move(node)) { }

	void subscribe(const LuaEventDescriptor& descriptor, const luabridge::LuaRef& callback, lua_State* state);
	void unsubscribe(const LuaEventDescriptor& descriptor);
	void unsubscribe(const LuaEventDescriptor& descriptor, const std::string& method);

private:
	toast::Box<toast::Node> m_node;
};

void clearLuaEventSubscriptions(void* runtime) noexcept;
void clearAllLuaEventSubscriptions() noexcept;

}
