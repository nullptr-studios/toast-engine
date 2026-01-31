/**
 * @file GameEvents.hpp
 * @author Dante Harper
 * @date 31/01/26
 *
 * @brief [TODO: Brief description of the file's purpose]
 */

#pragma once

#include "Toast/Event/Event.hpp"

namespace toast {
struct LoadWorld : public event::IEvent {
	unsigned world;

	LoadWorld(unsigned world) : world(world) { }
};

struct LoadLevel : public event::IEvent {
	unsigned world;
	unsigned level;

	LoadLevel(unsigned world, unsigned level) : world(world), level(level) { }
};

struct NextWorld : public event::IEvent { };

struct NextLevel : public event::IEvent { };
}
