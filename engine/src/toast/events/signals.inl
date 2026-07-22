#pragma once
#include "signals.hpp"

namespace signals {

template<typename NodeType, typename... Args>
template<typename F>
  requires SignalCallback<F, NodeType, Args...>
inline void Signal<NodeType, Args...>::subscribe(toast::Node& node, F&& cb) {
	callback_t wrapper = [f = std::forward<F>(cb)](NodeType& n, Args... args) mutable {
		if constexpr (std::is_invocable_r_v<void, F, NodeType&, Args...>) {
			f(n, args...);
		} else if constexpr (std::is_invocable_r_v<void, F, NodeType&>) {
			f(n);
		} else if constexpr (std::is_invocable_r_v<void, F, Args...>) {
			f(args...);
		} else if constexpr (std::is_invocable_r_v<void, F>) {
			f();
		}
	};
	m.listeners.push_back({
	  .node = toast::Box<toast::Node>(node),    //
	  .identifier = "Unnamed",
	  .cb = std::move(wrapper)                  //
	});
}

template<typename NodeType, typename... Args>
inline void Signal<NodeType, Args...>::subscrive(toast::Node& node, std::string_view identifier) {
	callback_t wrapper = [name = std::string(identifier), box = toast::Box<toast::Node>(node)](NodeType& n, Args... args) {
		box->call(name, n, args...);
	};
	m.listeners.push_back({
	  .node = toast::Box<toast::Node>(node),    //
	  .identifier = std::string(identifier),
	  .cb = std::move(wrapper)                  //
	});
}

template<typename NodeType, typename... Args>
inline void Signal<NodeType, Args...>::fire(NodeType& source_node, Args... args) {
	std::erase_if(m.listeners, [](const SigGroup& listener) {
		return not listener.node;    //
	});
	for (auto& listener : m.listeners) {
		if (listener.node.enabled()) {
			listener.cb(source_node, args...);
		}
	}
}
}
