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
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, bool& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, int64_t& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, double& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, std::string& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, toast::UID& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, toast::Box<toast::Node>& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec2& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec3& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::vec4& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int table, std::string_view field, glm::quat& out) -> bool;

TOAST_API void pushEventPrimitive(lua_State* state, bool value);
TOAST_API void pushEventPrimitive(lua_State* state, int64_t value);
TOAST_API void pushEventPrimitive(lua_State* state, double value);
TOAST_API void pushEventPrimitive(lua_State* state, std::string_view value);
TOAST_API void pushEventPrimitive(lua_State* state, toast::UID value);
TOAST_API void pushEventPrimitive(lua_State* state, const toast::Box<toast::Node>& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec2& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec3& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec4& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::quat& value);
TOAST_API void pushEventTable(lua_State* state, int field_count);
TOAST_API void setEventTableField(lua_State* state, std::string_view field);

template<typename T>
auto readEventValue(lua_State* state, int table, std::string_view field, T& out, std::string& error) -> bool {
	bool ok = false;
	if constexpr (std::same_as<T, bool>) {
		ok = readEventPrimitive(state, table, field, out);
	} else if constexpr (std::is_enum_v<T> || std::integral<T>) {
		int64_t value = 0;
		ok = readEventPrimitive(state, table, field, value);
		if (ok) {
			out = static_cast<T>(value);
		}
	} else if constexpr (std::floating_point<T>) {
		double value = 0.0;
		ok = readEventPrimitive(state, table, field, value);
		if (ok) {
			out = static_cast<T>(value);
		}
	} else {
		ok = readEventPrimitive(state, table, field, out);
	}
	if (!ok) {
		error = "invalid or missing field '" + std::string(field) + "'";
	}
	return ok;
}

template<typename T>
void pushEventValue(lua_State* state, const T& value) {
	if constexpr (std::same_as<T, bool>) {
		pushEventPrimitive(state, value);
	} else if constexpr (std::is_enum_v<T> || std::integral<T>) {
		pushEventPrimitive(state, static_cast<int64_t>(value));
	} else if constexpr (std::floating_point<T>) {
		pushEventPrimitive(state, static_cast<double>(value));
	} else if constexpr (std::same_as<T, std::string>) {
		pushEventPrimitive(state, std::string_view(value));
	} else {
		pushEventPrimitive(state, value);
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
	if constexpr (std::is_constructible_v<Event, decltype(std::move(std::get<I>(values)))...>) {
		std::apply([](auto&&... args) { event::send<Event>(std::move(args)...); }, values);
	} else {
		Event built {};
		((built.*(std::get<I>(fields).member) = std::move(std::get<I>(values))), ...);
		event::send<Event>(std::move(built));
	}
	return true;
}

template<typename Event, typename... Fields, size_t... I>
void pushEvent(lua_State* state, const Event& value, const std::tuple<Fields...>& fields, std::index_sequence<I...>) {
	pushEventTable(state, static_cast<int>(sizeof...(Fields)));
	((pushEventValue(state, value.*(std::get<I>(fields).member)), setEventTableField(state, std::get<I>(fields).name)), ...);
}
}

template<typename Event, typename... Fields>
void registerLuaEvent(std::string_view name, Fields... fields) {
	using Values = std::tuple<std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Event>().*fields.member)>>...>;
	static_assert(
	    std::is_default_constructible_v<Event> ||
	        []<typename... T>(std::tuple<T...>*) { return std::is_constructible_v<Event, T...>; }(static_cast<Values*>(nullptr)),
	    "Lua events need a constructor taking the public fields in order, or a default constructor"
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
