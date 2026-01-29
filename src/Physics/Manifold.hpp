/// @file Manifold.hpp
/// @author Xein
/// @date 1 Jan 2026

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Manifold {
	glm::dvec2 normal;
	glm::dvec2 contact1;
	glm::dvec2 contact2;
	int contactCount;
	double depth;

	void Debug() const;
};

struct BoxManifold {
	glm::dvec2 normal;
	glm::dvec2 contact1;
	glm::dvec2 contact2;
	int contactCount;
	double depth;
	int colliderEdgeIndex;
	int boxEdgeIndex;

	void Debug() const;
};

}
