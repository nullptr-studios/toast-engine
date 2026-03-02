/// @file Collider.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "ColliderData.hpp"
#include "ColliderFlags.hpp"
#include "Line.hpp"
#include "Toast/Components/Component.hpp"

#include <glm/glm.hpp>

namespace toast {
class Actor;
}

namespace physics {

class ConvexCollider;

/// <summary>
/// What mode for the points to appear on the editor
/// </summary>
enum class ColliderEditMode {
	VERTICES,
	EDGES,
	MULTI,
};

class Collider : public toast::Component {
public:
	REGISTER_TYPE(Collider);

#ifdef TOAST_EDITOR
	void Inspector() override;
#endif
	void EditorTick() override;
	void Destroy() override;

	json_t Save() const override;
	void Load(json_t j, bool propagate) override;
	void Init() override;

	void CalculatePoints();
	void Bevel(unsigned idx);

	template<typename... Args>
	void AddPoints(Args... points);
	void AddPoint(glm::vec2 point);
	void AddPointAt(int index, glm::vec2 point);
	void SwapPoints(glm::vec2 lhs, glm::vec2 rhs);
	void DeletePoint(glm::vec2 point);
	void DeleteAt(unsigned idx);

	auto GetPoints() -> std::list<glm::vec2>& {
		return m.points;
	}

	auto GetEdges() -> std::list<Line> {
		return m.edges;
	}

	auto currentEditMode() -> ColliderEditMode {
		return m.currentEditMode;
	}

	ColliderData data;

private:
	struct {
		std::vector<ConvexCollider*> convexShapes;
		std::list<glm::vec2> points;
		std::list<Line> edges;
		ColliderFlags flags = ColliderFlags::Default;
		ColliderEditMode currentEditMode = ColliderEditMode::VERTICES;
	} m;

	struct {
		bool showPoints = true;
		bool showColliders = true;
		glm::vec2 newPointPosition;
		glm::mat4 oldPosition;
		int bevelSubdivisions = 1;
	} debug;
};

template<typename... Args>
void Collider::AddPoints(Args... points) {
	(AddPoint(points), ...);
}

}
