/// @file Collider.hpp
/// @author Xein
/// @date 25 Dec 2025

#pragma once
#include "Line.hpp"

#include <Engine/Toast/Components/Component.hpp>
#include <glm/glm.hpp>

namespace physics {

class Collider : public toast::Component {
public:
	REGISTER_TYPE(Collider);

	void Inspector() override;
	void EditorTick() override;

	void Init() override;
	void Destroy() override;

	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	void AddPoint(glm::vec2 position = { 0.0f, 0.0f });
	void SetPoints(const std::vector<glm::vec2>& points);
	auto GetPoint(std::size_t index) const -> glm::vec2;
	auto GetLines() const -> const std::vector<Line>&;
	auto GetLineCount() const -> std::size_t;
	void ClearPoints();

private:
	void SwapPoints(std::size_t a, std::size_t b);
	void CalculateLines();

	struct M {
		std::vector<glm::vec2> points;
		std::vector<Line> lines;
		glm::vec2 newPointPosition { 0.0f, 0.0f };
		int draggingIndex = -1;
		float gizmoRadius = 8.0f;
	} m;
};

}
