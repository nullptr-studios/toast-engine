/**
 * @file destruction_event.hpp
 * @author Xein
 * @date 2 Oct 2026
 * @brief Event sent when voxels get destroyed
 */

#pragma once

#include <glm/vec3.hpp>
#include <toast/events/event.hpp>

namespace event {

struct DestructionEvent : public Event<DestructionEvent> {
	glm::vec3 position;
	int voxel_count;

	DestructionEvent(glm::vec3 position, int voxel_count) : position(position), voxel_count(voxel_count) { }
};

}
