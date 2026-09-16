#pragma once
#include "signals.hpp"
#include "toast/world/node.hpp"

namespace signals {

template<typename... Args>
template<typename F>
  requires SignalCallback<F, Args...>
inline void Signal<Args...>::subscribe(toast::Node& node, F&& cb) {
	callback_t wrapper = [f = std::forward<F>(cb)](Args... args) mutable {
		if constexpr (std::is_invocable_r_v<void, F, Args...>) {
			f(args...);
		} else if constexpr (std::is_invocable_r_v<void, F>) {
			f();
		}
	};
	m.listeners.push_back(
	    {.uid = node.uid(),
			 .identifier = "Unnamed",
			 .source = ConnectionSource::cpp,
			 .node = toast::Box<toast::Node>(node),
			 .cb = std::move(wrapper)}
	);
}

template<typename... Args>
inline void Signal<Args...>::subscribe(toast::Node& node, std::string_view identifier) {
	subscribe(node, identifier, ConnectionSource::cpp, true);
}

template<typename... Args>
inline void
    Signal<Args...>::subscribe(toast::Node& node, std::string_view identifier, ConnectionSource source, bool forwards_args) {
	callback_t wrapper;
	if (forwards_args) {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](Args... args) mutable {
			box->call(iden, args...);
		};
	} else {
		wrapper = [iden = std::string(identifier), box = toast::Box<toast::Node>(node)](Args...) mutable { box->call(iden); };
	}
	m.listeners.push_back(
	    {.uid = node.uid(),
			 .identifier = std::string(identifier),
			 .source = source,
			 .forwards_args = forwards_args,
			 .node = toast::Box<toast::Node>(node),
			 .cb = std::move(wrapper)}
	);
}

template<typename... Args>
inline void Signal<Args...>::unsubscribe(toast::Node& node, std::string_view identifier) {
	std::erase_if(m.listeners, [&](const SigGroup& listener) {
		return listener.node == toast::Box<toast::Node>(node) && listener.identifier == identifier;
	});
}

template<typename... Args>
inline void Signal<Args...>::unsubscribe(toast::Node& node, std::string_view identifier, ConnectionSource source) {
	std::erase_if(m.listeners, [&](const SigGroup& listener) {
		return listener.node == toast::Box<toast::Node>(node) && listener.identifier == identifier && listener.source == source;
	});
}

template<typename... Args>
inline void Signal<Args...>::clear(ConnectionSource source) {
	std::erase_if(m.listeners, [source](const SigGroup& listener) { return listener.source == source; });
}

template<typename... Args>
inline auto Signal<Args...>::connections() const -> std::vector<ConnectionInfo> {
	std::vector<ConnectionInfo> result;
	result.reserve(m.listeners.size());
	for (const auto& listener : m.listeners) {
		result.push_back({listener.uid, listener.identifier, listener.source, listener.forwards_args});
	}
	return result;
}

template<typename... Args>
inline auto Signal<Args...>::has(toast::UID node, std::string_view identifier) const -> bool {
	return std::ranges::any_of(m.listeners, [&](const SigGroup& listener) {
		return listener.uid == node && listener.identifier == identifier;
	});
}

template<typename... Args>
inline void Signal<Args...>::fire(Args... args) {
	std::erase_if(m.listeners, [](const SigGroup& listener) {
		return not listener.node;    //
	});
	for (auto& listener : m.listeners) {
		if (listener.node.enabled()) {
			listener.cb(args...);
		}
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
inline void Signal<Args...>::connect(void* signal, toast::Node& target, std::string_view identifier, bool forwards_args) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	if (!target_signal.has(target.uid(), identifier)) {
		target_signal.subscribe(target, identifier, ConnectionSource::editor, forwards_args);
	}
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline void Signal<Args...>::disconnect(void* signal, toast::Node& target, std::string_view identifier) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	target_signal.unsubscribe(target, identifier, ConnectionSource::editor);
}

template<typename... Args>
template<typename NodeType, auto MemberPtr>
inline void Signal<Args...>::clearEditor(void* signal) {
	if (!signal) {
		return;
	}
	auto& target_signal = static_cast<NodeType*>(signal)->*MemberPtr;
	target_signal.clear(ConnectionSource::editor);
}

}
