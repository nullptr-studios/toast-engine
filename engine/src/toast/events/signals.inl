#pragma once
#include "signals.hpp"

#include <toast/world/node.hpp>
#include <tracy/Tracy.hpp>

namespace signals {

template<typename... Args>
template<typename F>
  requires(std::is_invocable_r_v<void, F, Args...> || std::is_invocable_r_v<void, F>)
inline void Signal<Args...>::connect(toast::Node& node, F&& cb) {
	callback_t wrapper = [f = std::forward<F>(cb)](Args... args) mutable {
		if constexpr (std::is_invocable_r_v<void, F, Args...>) {
			f(args...);
		} else if constexpr (std::is_invocable_r_v<void, F>) {
			f();
		}
	};
	m_connections.push_back(
	    {.uid = node.uid(),
	     .identifier = "Unnamed",
	     .source = ConnectionSource::cpp,
	     .node = toast::Box<toast::Node>(node),
	     .cb = std::move(wrapper)}
	);
}

template<typename... Args>
inline void
    Signal<Args...>::connect(toast::Node& node, std::string_view identifier, ConnectionSource source, bool forwards_args) {
	callback_t wrapper;
	if (forwards_args) {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](const Args&... args) mutable {
			box->call(iden, args...);
		};
	} else {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](const Args&...) mutable { box->call(iden); };
	}
	m_connections.push_back(
	    {.uid = node.uid(),
	     .identifier = std::string(identifier),
	     .source = source,
	     .forwards_args = forwards_args,
	     .node = toast::Box<toast::Node>(node),
	     .cb = std::move(wrapper)}
	);
}

template<typename... Args>
inline void Signal<Args...>::disconnect(toast::Node& node, std::string_view identifier, ConnectionSource source) {
	std::erase_if(m_connections, [&](const Connection& listener) {
		return listener.node == toast::Box<toast::Node>(node) && listener.identifier == identifier && listener.source == source;
	});
}

template<typename... Args>
inline void Signal<Args...>::clear(ConnectionSource source) {
	std::erase_if(m_connections, [source](const Connection& listener) { return listener.source == source; });
}

template<typename... Args>
inline auto Signal<Args...>::connections() const -> std::vector<ConnectionInfo> {
	std::vector<ConnectionInfo> result;
	result.reserve(m_connections.size());
	for (const auto& listener : m_connections) {
		result.push_back({listener.uid, listener.identifier, listener.source, listener.forwards_args});
	}
	return result;
}

template<typename... Args>
inline void Signal<Args...>::fire(const Args&... args) {
	ZoneScoped;
	std::erase_if(m_connections, [](const Connection& listener) { return !listener.node; });
	for (auto& listener : m_connections) {
		listener.cb(args...);
	}
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline auto Signal<Args...>::get(void* signal) -> std::vector<ConnectionInfo> {
	if (!signal) {
		return {};
	}
	auto* node = static_cast<NodeType*>(signal);
	const auto& target_signal = node->*MemberPtr;
	return target_signal.connections();
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline void Signal<Args...>::connect(
    void* signal, toast::Node& target, std::string_view identifier, ConnectionSource source, bool forwards_args
) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	const auto connections = target_signal.connections();
	const bool exists = std::ranges::any_of(connections, [&](const ConnectionInfo& connection) {
		return connection.target == target.uid() && connection.function == identifier && connection.source == source;
	});
	if (!exists) {
		target_signal.connect(target, identifier, source, forwards_args);
	}
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline void Signal<Args...>::disconnect(void* signal, toast::Node& target, std::string_view identifier, ConnectionSource source) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	target_signal.disconnect(target, identifier, source);
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline void Signal<Args...>::clear(void* signal, ConnectionSource source) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	target_signal.clear(source);
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline auto Signal<Args...>::fire(void* signal, std::span<const std::any> args) -> bool {
	ZoneScoped;
	if (!signal || args.size() != sizeof...(Args)) {
		return false;
	}
	return [&]<size_t... I>(std::index_sequence<I...>) {
		if ((std::any_cast<std::decay_t<Args>>(&args[I]) && ...)) {
			(static_cast<NodeType*>(signal)->*MemberPtr).fire(*std::any_cast<std::decay_t<Args>>(&args[I])...);
			return true;
		}
		return false;
	}(std::index_sequence_for<Args...> {});
}

}
