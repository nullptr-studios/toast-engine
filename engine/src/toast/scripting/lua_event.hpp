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
#include <toast/assets/core_types.hpp>
#include <toast/events/listener.hpp>
#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <toast/world/box.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace toast {
class Node;
}

namespace input {
class Action;
}

namespace physics {
struct BroadPhasePair;
}

namespace event {
struct ContactEventData;
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

template<typename Owner, typename Get, typename Set>
struct LuaEventField {
	using value_type = std::remove_cvref_t<std::invoke_result_t<const Get&, const Owner&>>;

	std::string_view name;
	std::string_view type;
	Get get;
	Set set;
};

template<typename Owner, typename Get, typename Set>
constexpr auto luaEventField(std::string_view name, std::string_view type, Get get, Set set) -> LuaEventField<Owner, Get, Set> {
	return {name, type, std::move(get), std::move(set)};
}

template<typename Owner, typename Member>
constexpr auto luaEventField(std::string_view name, Member Owner::* member) {
	return luaEventField<Owner>(
	    name,
	    {},
	    [member](const Owner& owner) -> const Member& { return owner.*member; },
	    [member](Owner& owner, Member value) { owner.*member = std::move(value); }
	);
}

namespace _detail {
TOAST_API auto readEventPrimitive(lua_State* state, int index, bool& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, int64_t& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, double& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, std::string& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, toast::UID& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, toast::Box<toast::Node>& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, toast::Node*& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::vec2& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::vec3& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::vec4& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::ivec2& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::ivec3& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::ivec4& out) -> bool;
TOAST_API auto readEventPrimitive(lua_State* state, int index, glm::quat& out) -> bool;
TOAST_API auto readEventHandle(lua_State* state, int index, std::string_view type, assets::HandleBase& out) -> bool;

TOAST_API void pushEventPrimitive(lua_State* state, bool value);
TOAST_API void pushEventPrimitive(lua_State* state, int64_t value);
TOAST_API void pushEventPrimitive(lua_State* state, double value);
TOAST_API void pushEventPrimitive(lua_State* state, std::string_view value);
TOAST_API void pushEventPrimitive(lua_State* state, toast::UID value);
TOAST_API void pushEventPrimitive(lua_State* state, const toast::Box<toast::Node>& value);
TOAST_API void pushEventPrimitive(lua_State* state, const toast::Node* value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec2& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec3& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::vec4& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::ivec2& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::ivec3& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::ivec4& value);
TOAST_API void pushEventPrimitive(lua_State* state, const glm::quat& value);
TOAST_API void pushEventPrimitive(lua_State* state, const assets::HandleBase& value);
TOAST_API void pushEventPrimitive(lua_State* state, const input::Action& value);
TOAST_API void pushEventPrimitive(lua_State* state, const physics::BroadPhasePair& value);
TOAST_API void pushEventPrimitive(lua_State* state, const event::ContactEventData& value);
TOAST_API void pushEventTable(lua_State* state, int field_count);
TOAST_API void setEventTableField(lua_State* state, std::string_view field);

TOAST_API auto pushEventField(lua_State* state, int table, std::string_view field) -> int;
TOAST_API auto pushEventElement(lua_State* state, int table, int64_t i) -> int;
TOAST_API void popEventValue(lua_State* state);
TOAST_API auto eventArrayLength(lua_State* state, int index) -> int64_t;
TOAST_API void pushEventArray(lua_State* state, size_t size);
TOAST_API void setEventArrayElement(lua_State* state, int64_t i);

template<typename T>
inline constexpr bool is_vector_v = false;
template<typename T, typename Alloc>
inline constexpr bool is_vector_v<std::vector<T, Alloc>> = true;

template<typename T>
inline constexpr bool is_handle_v = false;
template<typename T>
inline constexpr bool is_handle_v<assets::Handle<T>> = true;

template<typename T>
auto readEventIndex(lua_State* state, int index, std::string_view type, T& out) -> bool {
	if constexpr (!std::same_as<T, bool> && (std::is_enum_v<T> || std::integral<T>)) {
		int64_t value = 0;
		if (!readEventPrimitive(state, index, value)) {
			return false;
		}
		out = static_cast<T>(value);
		return true;
	} else if constexpr (std::floating_point<T>) {
		double value = 0.0;
		if (!readEventPrimitive(state, index, value)) {
			return false;
		}
		out = static_cast<T>(value);
		return true;
	} else if constexpr (is_vector_v<T>) {
		const int64_t count = eventArrayLength(state, index);
		if (count < 0) {
			return false;
		}
		out.clear();
		out.reserve(static_cast<size_t>(count));
		for (int64_t i = 1; i <= count; ++i) {
			typename T::value_type element {};
			const bool ok = readEventIndex(state, pushEventElement(state, index, i), type, element);
			popEventValue(state);
			if (!ok) {
				return false;
			}
			out.push_back(std::move(element));
		}
		return true;
	} else if constexpr (is_handle_v<T>) {
		assets::HandleBase handle;
		if (!readEventHandle(state, index, type, handle)) {
			return false;
		}
		out = T(handle.hasValue() ? handle.operator->() : nullptr, handle.uid(), handle.path());
		return true;
	} else {
		return readEventPrimitive(state, index, out);
	}
}

template<typename T>
auto readEventValue(lua_State* state, int table, std::string_view field, std::string_view type, T& out, std::string& error)
    -> bool {
	const bool ok = readEventIndex(state, pushEventField(state, table, field), type, out);
	popEventValue(state);
	if (!ok) {
		error = "invalid or missing field '" + std::string(field) + "'";
	}
	return ok;
}

template<typename T>
void pushEventValue(lua_State* state, const T& value) {
	if constexpr (!std::same_as<T, bool> && (std::is_enum_v<T> || std::integral<T>)) {
		pushEventPrimitive(state, static_cast<int64_t>(value));
	} else if constexpr (std::floating_point<T>) {
		pushEventPrimitive(state, static_cast<double>(value));
	} else if constexpr (std::same_as<T, std::string>) {
		pushEventPrimitive(state, std::string_view(value));
	} else if constexpr (is_vector_v<T>) {
		pushEventArray(state, value.size());
		int64_t i = 0;
		for (const auto& element : value) {
			pushEventValue<typename T::value_type>(state, element);
			setEventArrayElement(state, ++i);
		}
	} else if constexpr (is_handle_v<T>) {
		pushEventPrimitive(state, static_cast<const assets::HandleBase&>(value));
	} else {
		pushEventPrimitive(state, value);
	}
}

template<typename Event, typename... Fields, size_t... I>
auto sendEvent(lua_State* state, int payload, std::string& error, const std::tuple<Fields...>& fields, std::index_sequence<I...>)
    -> bool {
	std::tuple<typename Fields::value_type...> values;
	const bool valid =
	    (readEventValue(state, payload, std::get<I>(fields).name, std::get<I>(fields).type, std::get<I>(values), error) && ...);
	if (!valid) {
		return false;
	}
	if constexpr (std::is_constructible_v<Event, decltype(std::move(std::get<I>(values)))...>) {
		std::apply([](auto&&... args) { event::send<Event>(std::move(args)...); }, values);
	} else {
		Event built {};
		(std::get<I>(fields).set(built, std::move(std::get<I>(values))), ...);
		event::send<Event>(std::move(built));
	}
	return true;
}

template<typename Event, typename... Fields, size_t... I>
void pushEvent(lua_State* state, const Event& value, const std::tuple<Fields...>& fields, std::index_sequence<I...>) {
	pushEventTable(state, static_cast<int>(sizeof...(Fields)));
	((pushEventValue(state, std::get<I>(fields).get(value)), setEventTableField(state, std::get<I>(fields).name)), ...);
}
}

/**
 * Exposes @c Event to Lua as @c Events.<name>
 * @tparam Sendable false for events lua cannot send (receive only)
 */
template<typename Event, bool Sendable = true, typename... Fields>
void registerLuaEvent(std::string_view name, Fields... fields) {
	if constexpr (Sendable) {
		static_assert(
		    std::is_default_constructible_v<Event> || std::is_constructible_v<Event, typename Fields::value_type...>,
		    "Lua events need a constructor taking the public fields in order, or a default constructor"
		);
	}
	auto metadata = std::make_shared<std::tuple<Fields...>>(fields...);
	auto binding = std::make_shared<LuaEventBinding>();
	binding->name = name;
	if constexpr (Sendable) {
		binding->send = [metadata](lua_State* state, int payload, std::string& error) {
			return _detail::sendEvent<Event>(state, payload, error, *metadata, std::index_sequence_for<Fields...> {});
		};
	}
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
