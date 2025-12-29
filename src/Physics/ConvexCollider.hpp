/// @file ConvexCollider.hpp
/// @author Xein
/// @date 29 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Line {
	glm::vec2 p1;
	glm::vec2 p2;
	glm::dvec2 normal;
	glm::dvec2 tangent;
	double length;
};

class ConvexCollider {
public:
	using point_list = std::list<std::pair<glm::vec2, bool>>;
	explicit ConvexCollider(const point_list& points);
	~ConvexCollider();

	std::vector<Line> edges;
	std::vector<glm::dvec2> vertices;

	void Debug();
};

}
