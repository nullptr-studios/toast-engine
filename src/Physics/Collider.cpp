#include <Engine/Physics/Collider.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>
#include <Engine/Toast/Objects/Actor.hpp>
#include <imgui.h>

using namespace physics;

void Collider::Inspector() {
	ImGui::TextUnformatted("Points");
	ImGui::Separator();

	// New point UI
	ImGui::DragFloat2("New Point (world)", &m.newPointPosition.x, 0.1f);
	if (ImGui::Button("Add Point")) {
		AddPoint(m.newPointPosition);
	}

	ImGui::Spacing();
	ImGui::Separator();

	// Existing points UI
	for (std::size_t i = 0; i < m.points.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("Point %zu", i);

		// Edit position
		ImGui::DragFloat2("Position", &m.points[i].x, 0.1f);

		ImGui::SameLine();
		if (ImGui::SmallButton("U") && i > 0) {
			SwapPoints(i, i - 1);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("D") && i + 1 < m.points.size()) {
			SwapPoints(i, i + 1);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) {
			m.points.erase(m.points.begin() + i);
			ImGui::PopID();
			break;
		}

		ImGui::Separator();
		ImGui::PopID();
	}
}

void Collider::EditorTick() {
	for (size_t i = 0; i < m.points.size(); ++i) {
		renderer::DebugLine(m.points[i], m.points[(i + 1) % m.points.size()]);
	}
}

void Collider::AddPoint(glm::vec2 position) {
	// Points are stored in world space; add relative to actor world position for convenience.
	const auto world = static_cast<toast::Actor*>(parent())->transform()->worldPosition();
	m.points.emplace_back(position + static_cast<glm::vec2>(world));
}

void Collider::SetPoints(const std::vector<glm::vec2>& points) {
	m.points = points;
}

void Collider::ClearPoints() {
	m.points.clear();
	m.draggingIndex = -1;
}

void Collider::SwapPoints(std::size_t a, std::size_t b) {
	if (a < m.points.size() && b < m.points.size()) {
		std::swap(m.points[a], m.points[b]);
	}
}
