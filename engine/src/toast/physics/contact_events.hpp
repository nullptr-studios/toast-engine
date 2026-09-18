/**
 * @file contact_events.hpp
 * @brief Physics contact lifecycle events
 */

#pragma once

#include "manifold.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <toast/events/event.hpp>
#include <toast/export.hpp>

namespace event {

struct ContactEventData {
	physics::BroadPhasePair pair;
	glm::vec3 normal = {};
	std::array<physics::ContactPoint, 4> contacts = {};
	uint8_t contact_count = 0;

	explicit ContactEventData(const physics::Manifold& manifold)
	    : pair(manifold.pair),
	      normal(manifold.normal),
	      contact_count(static_cast<uint8_t>(std::min<size_t>(manifold.contact_count, contacts.size()))) {
		const size_t count = contact_count;
		for (size_t index = 0; index < count; ++index) {
			contacts[index] = manifold.contacts[index];
		}
	}
};

struct TOAST_API ContactBegin : Event<ContactBegin> {
	explicit ContactBegin(const physics::Manifold& manifold) : contact(manifold) { }

	ContactEventData contact;
};

struct TOAST_API ContactPersist : Event<ContactPersist> {
	explicit ContactPersist(const physics::Manifold& manifold) : contact(manifold) { }

	ContactEventData contact;
};

struct TOAST_API ContactEnd : Event<ContactEnd> {
	explicit ContactEnd(physics::BroadPhasePair pair) : pair(pair) { }

	physics::BroadPhasePair pair;
};

}
