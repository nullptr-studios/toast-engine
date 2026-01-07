/// @file PhysicsEvents.hpp
/// @author Xein
/// @date 30 Dec 2025

#pragma once
#include <Toast/Event/Event.hpp>
#include <glm/glm.hpp>

namespace physics {

struct UpdatePhysicsDefaults : event::Event<UpdatePhysicsDefaults> {
	glm::dvec2 gravity;
	double positionCorrectionPtc;
	double positionCorrectionSlop;
	double eps;
	double epsSmall;
	unsigned iterationCount;

	UpdatePhysicsDefaults(glm::dvec2 g, double ptc, double slop, double eps, double eps_small, unsigned it)
	    : gravity(g),
	      positionCorrectionSlop(slop),
	      positionCorrectionPtc(ptc),
	      eps(eps),
	      epsSmall(eps_small),
	      iterationCount(it) { }
};

}
