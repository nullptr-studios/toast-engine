/// @file Collider.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace toast {
class Actor;
}

namespace physics {

class ConvexCollider;

class Collider : public toast::Component {
public:
	REGISTER_TYPE(Collider);

	void Inspector() override;
	void EditorTick() override;
	void Destroy() override;

	void CalculatePoints();

	template<typename... Args>
	void AddPoints(Args... points);
	void AddPoint(glm::vec2 point);
	void SwapPoints(glm::vec2 lhs, glm::vec2 rhs);
	void DeletePoint(glm::vec2 point);

private:
	float ShoelaceArea();

	struct {
		std::vector<ConvexCollider*> convexShapes;
		std::list<glm::vec2> points;
		glm::vec2 newPointPosition;
	} m;
};

template<typename... Args>
void Collider::AddPoints(Args... points) {
	(AddPoint(points), ...);
}

}
