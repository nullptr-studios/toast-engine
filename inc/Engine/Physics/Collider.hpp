#pragma once
#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class Collider : public toast::Component {
public:
	REGISTER_TYPE(Collider);

	void Inspector() override;
	void EditorTick() override;

	void AddPoint(glm::vec2 position = {0.0f, 0.0f});
	void SetPoints(const std::vector<glm::vec2>& points);
	const std::vector<glm::vec2>& GetPoints() const { return m.points; }
	void ClearPoints();

private:
	void SwapPoints(std::size_t a, std::size_t b);

	struct M {
		std::vector<glm::vec2> points;
		glm::vec2 newPointPosition {0.0f, 0.0f};
		int draggingIndex = -1;
		float gizmoRadius = 8.0f;
	} m;
};

}
