/// @file Collider.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "ColliderData.hpp"
#include "../src/Physics/ColliderFlags.hpp"
#include "Toast/Components/Component.hpp"

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

	json_t Save() const override;
	void Load(json_t j, bool propagate) override;

	void CalculatePoints();

	template<typename... Args>
	void AddPoints(Args... points);
	void AddPoint(glm::vec2 point);
	void SwapPoints(glm::vec2 lhs, glm::vec2 rhs);
	void DeletePoint(glm::vec2 point);

	ColliderData data;

private:
	struct {
		std::vector<ConvexCollider*> convexShapes;
		std::list<glm::vec2> points;
		ColliderFlags flags{};
	} m;

	struct {
		bool showPoints = true;
		bool showColliders = true;
		glm::vec2 newPointPosition;
	} debug;
};

template<typename... Args>
void Collider::AddPoints(Args... points) {
	(AddPoint(points), ...);
}

}
