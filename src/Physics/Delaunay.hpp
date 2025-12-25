#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace physics {

struct Triangle {
	int a, b, c;
};

struct Edge {
	int a, b;

	bool operator==(const Edge& other) const {
		return (a == other.a && b == other.b) || (a == other.b && b == other.a);
	}
};

std::vector<Triangle> delaunayTriangulate(const std::vector<glm::dvec2>& pts, double eps = 1e-12);

}
