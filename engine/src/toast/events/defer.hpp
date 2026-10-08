/**
 * @file defer.hpp
 * @author Dante Harper
 * @date 30 Sep 26
 */

#pragma once

#include "toast/events/event.hpp"
#include "toast/export.hpp"

#include <functional>

namespace toast {

namespace _detail {

struct TOAST_API Defer : event::Event<Defer> {
	std::function<void()> cb;

	Defer(std::function<void()>&& fn) : cb(std::move(fn)) { }
};

}

inline TOAST_API void defer(std::function<void()> fn) {
	event::send<_detail::Defer>(std::move(fn));
}
}
