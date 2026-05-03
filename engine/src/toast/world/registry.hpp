/**
 * @file registry.hpp
 * @author Dante Harper
 * @date 29 Apr 26
 */

#pragma once

#include <functional>
#include <toast/export.hpp>
#include <vector>

namespace toast {
class Node;

struct TOAST_API NodeVTable {
	// std::vector<std::function<void(Node*)>> save;            // saving data
	// std::vector<std::function<void(Node*)>> load;            // loading data
	std::vector<std::function<void(Node*)>> pre_init;        // loading resources pre-object creation
	std::vector<std::function<void(Node*)>> init;            // loading resources
	std::vector<std::function<void(Node*)>> begin;           // moved from cache
	std::vector<std::function<void(Node*)>> on_enable;       // on enable
	std::vector<std::function<void(Node*)>> early_tick;      // start of a frame
	std::vector<std::function<void(Node*)>> tick;            // before the physics tick
	std::vector<std::function<void(Node*)>> post_physics;    // after the physics tick
	std::vector<std::function<void(Node*)>> on_disable;      // on disable
	std::vector<std::function<void(Node*)>> end;             // moved to cache
	std::vector<std::function<void(Node*)>> destroy;         // moved to destroy
};
}
