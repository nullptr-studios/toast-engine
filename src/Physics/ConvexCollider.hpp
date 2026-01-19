/// @file ConvexCollider.hpp
/// @author Xein
/// @date 29 Dec 2025

#pragma once
#include <Toast/Physics/ColliderData.hpp>
#include <Toast/Physics/Line.hpp>
#include <glm/glm.hpp>

namespace physics {

class ConvexCollider : public ColliderData {
public:
	using point_list = std::list<std::pair<glm::vec2, bool>>;
	explicit ConvexCollider(const point_list& points, const ColliderData& data);
	~ConvexCollider();

	std::vector<Line> edges;
	std::vector<glm::vec2> vertices;

	void Debug();
};

inline float ShoelaceArea(const std::list<glm::vec2>& points) {
	float area = 0.0f;

	for (auto it = points.begin(); it != points.end(); ++it) {
		auto it2 = std::next(it);
		if (it2 == points.end()) {
			it2 = points.begin();
		}
		glm::vec2 p1 = *it;
		glm::vec2 p2 = *it2;

		glm::mat2 mat { p1, p2 };
		area += glm::determinant(mat);
	}

	return area;
}

auto ConvexRayCollision(Line* ray, ConvexCollider* c) -> std::optional<std::pair<glm::dvec2, glm::dvec2>>;

}
