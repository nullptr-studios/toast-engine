/**
 * @file defer.hpp
 * @author Dante Harper
 * @date 30 Sep 26
 */

#pragma once

#include "toast/events/event.hpp"

#include <functional>

namespace toast {

namespace _detail {

struct Defer : event::Event<Defer> {
	std::function<void()> cb;

	Defer(std::function<void()>&& fn) : cb(std::move(fn)) { }
};

}

inline void defer(std::function<void()> fn) {
	event::send<_detail::Defer>(std::move(fn));
}
}
