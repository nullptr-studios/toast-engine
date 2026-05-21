/**
 * @file world_events.h
 * @author Xein
 * @date 18 May 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <toast/events/event.hpp>

namespace toast {
class Node;
}

namespace event {

struct SwapWorldRoot : Event<SwapWorldRoot> {
	SwapWorldRoot(toast::Node& node) : node(&node) { }

	toast::Node* node;
};

}
