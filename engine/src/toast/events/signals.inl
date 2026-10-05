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
	modify([&](ConnectionList& list) {
		list.push_back(
		    {.uid = node.uid(),
		     .identifier = "Unnamed",
		     .source = ConnectionSource::cpp,
		     .node = toast::Box<toast::Node>(node),
		     .cb = std::move(wrapper)}
		);
	});
}

template<typename... Args>
inline void
    Signal<Args...>::connect(toast::Node& node, std::string_view identifier, ConnectionSource source, bool forwards_args) {
	callback_t wrapper;
	if constexpr (std::is_same_v<std::tuple<Args...>, std::tuple<DynamicArgs>>) {
		wrapper =
		    [iden = std::string(identifier), box = toast::Box<toast::Node>(node), forwards_args](const DynamicArgs& args) mutable {
			    if (forwards_args && !args.values.empty()) {
				    toast::_detail::callNodeScripts(&*box, iden, args.values);
			    } else {
				    box->call(iden);
			    }
		    };
	} else if (forwards_args) {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](const Args&... args) mutable {
			box->call(iden, args...);
		};
	} else {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](const Args&...) mutable { box->call(iden); };
	}
	modify([&](ConnectionList& list) {
		list.push_back(
		    {.uid = node.uid(),
		     .identifier = std::string(identifier),
		     .source = source,
		     .forwards_args = forwards_args,
		     .node = toast::Box<toast::Node>(node),
		     .cb = std::move(wrapper)}
		);
	});
}

template<typename... Args>
inline void Signal<Args...>::disconnect(toast::Node& node, std::string_view identifier, ConnectionSource source) {
	modify([&](ConnectionList& list) {
		std::erase_if(list, [&](const Connection& listener) {
			return listener.node == toast::Box<toast::Node>(node) && listener.identifier == identifier && listener.source == source;
		});
	});
}

template<typename... Args>
inline void Signal<Args...>::clear(ConnectionSource source) {
	modify([source](ConnectionList& list) {
		std::erase_if(list, [source](const Connection& listener) { return listener.source == source; });
	});
}

template<typename... Args>
inline auto Signal<Args...>::connections() const -> std::vector<ConnectionInfo> {
	const auto current = snapshot();
	std::vector<ConnectionInfo> result;
	result.reserve(current->size());
	for (const auto& listener : *current) {
		result.push_back({listener.uid, listener.identifier, listener.source, listener.forwards_args});
	}
	return result;
}

template<typename... Args>
inline void Signal<Args...>::fire(const Args&... args) {
	ZoneScoped;
	// Handlers may connect, disconnect or fire this signal again
	const auto current = snapshot();
	bool has_dead = false;
	for (const auto& listener : *current) {
		if (!listener.node) {
			has_dead = true;
			continue;
		}
		listener.cb(args...);
	}
	if (has_dead) {
		modify([](ConnectionList& list) { std::erase_if(list, [](const Connection& listener) { return !listener.node; }); });
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
