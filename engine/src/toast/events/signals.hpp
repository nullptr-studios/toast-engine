/**
 * @file signals.hpp
 * @author Dante Harper
 * @date 17 Jul 26
 */

#pragma once

#include "toast/world/box.hpp"

#include <functional>

namespace toast {
class Node;
}

namespace signals {
namespace _detail { }

template<typename F, typename NodeType, typename... Args>
concept SignalCallback = std::is_invocable_r_v<void, F, NodeType&, Args...> ||    //
                         std::is_invocable_r_v<void, F, NodeType&> ||             //
                         std::is_invocable_r_v<void, F, Args...> ||               //
                         std::is_invocable_r_v<void, F>;                          //

template<typename NodeType, typename... Args>
class Signal {
	using callback_t = std::function<void(NodeType&, Args...)>;

	struct SigGroup {
		toast::Box<toast::Node> node;
		std::string identifier;
		callback_t cb;
	};

	struct {
		std::vector<SigGroup> listeners;
	} m;

public:
	template<typename F>
	  requires SignalCallback<F, NodeType, Args...>
	void subscribe(toast::Node& node, F&& cb);

	void subscrive(toast::Node& node, std::string_view identifier);

	void fire(NodeType& source_node, Args... args);
};

}
