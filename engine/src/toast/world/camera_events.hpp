/**
 * @file camera_events.h
 * @author Xein
 * @date 22 Jul 2026
 */

#pragma once
#include <toast/events/event.hpp>

namespace event {

struct SetActiveCamera : public Event<SetActiveCamera> {
	toast::Box<toast::Camera> camera;
};

}
