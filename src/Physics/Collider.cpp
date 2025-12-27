#include "Physics/PhysicsSystem.hpp"

#include <Engine/Core/GlmJson.hpp>
#include <Engine/Physics/Collider.hpp>
#include <Engine/Renderer/DebugDrawLayer.hpp>
#include <Engine/Toast/Objects/Actor.hpp>
#include <imgui.h>

using namespace physics;

void Collider::CalculateLines() {
	m.lines.clear();

	for (int i = 1; i <= m.points.size(); i++) {
		glm::vec2 tangent = glm::normalize(m.points[i % m.points.size()] - m.points[i - 1]);
		const glm::vec2 world = static_cast<toast::Actor*>(parent())->transform()->worldPosition();

		m.lines.emplace_back(
		    Line {
		      .point = m.points[i - 1] + world,
		      .tangent = tangent,
		      .normal = { -tangent.y, tangent.x },
		      .length = glm::distance(m.points[i % m.points.size()], m.points[i - 1])
    }
		);
	}
}

void Collider::Init() {
	PhysicsSystem::AddCollider(this);
}

json_t Collider::Save() const {
	json_t json = Component::Save();

	for (int i = 0; i < m.points.size(); i++) {
		json["points"][i] = m.points[i];
	}

	return json;
}

void Collider::Load(json_t j, bool force_create) {
	Component::Load(j, force_create);

	if (j.contains("points")) {
		for (const auto& j_point : j["points"]) {
			AddPoint(j_point);
		}
	}

	CalculateLines();
}

void Collider::Destroy() {
	PhysicsSystem::RemoveCollider(this);
}

void Collider::Inspector() {
	ImGui::TextUnformatted("Points");
	ImGui::Separator();

	// New point UI
	if (ImGui::Button("Add Point")) {
		AddPoint(m.newPointPosition);
		CalculateLines();
	}
	ImGui::DragFloat2("New Point (world)", &m.newPointPosition.x, 0.1f);

	ImGui::Separator();

	// Existing points UI
	for (std::size_t i = 0; i < m.points.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton("U") && i > 0) {
			SwapPoints(i, i - 1);
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("D") && i + 1 < m.points.size()) {
			SwapPoints(i, i + 1);
		}

		ImGui::SameLine();
		ImGui::Text("Point %zu", i);

		ImGui::SameLine();
		if (ImGui::SmallButton("X")) {
			m.points.erase(m.points.begin() + i);
			CalculateLines();
			ImGui::PopID();
			break;
		}

		if (ImGui::DragFloat2("Position", &m.points[i].x, 0.1f)) {
			CalculateLines();
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	if (ImGui::Button("Update lines")) {
		CalculateLines();
	}
}

void Collider::EditorTick() {
	const auto world = static_cast<toast::Actor*>(parent())->transform()->worldPosition();
	for (size_t i = 0; i < m.points.size(); ++i) {
		renderer::DebugLine(
		    m.points[i] + static_cast<glm::vec2>(world), m.points[(i + 1) % m.points.size()] + static_cast<glm::vec2>(world), { 0.0f, 1.0f, 0.0f, 1.0f }
		);
	}

	for (const auto& l : m.lines) {
		renderer::DebugLine(l.point, l.point + l.normal, { 1.0f, 0.0f, 0.0f, 0.5f });
	}
}

void Collider::AddPoint(glm::vec2 position) {
	// Points are stored in world space; add relative to actor world position for convenience.
	m.points.emplace_back(position);
	CalculateLines();
}

void Collider::SetPoints(const std::vector<glm::vec2>& points) {
	m.points = points;
	CalculateLines();
}

auto Collider::GetPoint(std::size_t index) const -> glm::vec2 {
	if (index >= m.points.size()) {
		return { 0.0f, 0.0f };
	}
	const auto world = static_cast<toast::Actor*>(parent())->transform()->worldPosition();
	return m.points[index] + static_cast<glm::vec2>(world);
}

auto Collider::GetLines() const -> const std::vector<Line>& {
	return m.lines;
}

std::size_t Collider::GetLineCount() const {
	return m.lines.size();
}

void Collider::ClearPoints() {
	m.points.clear();
	m.draggingIndex = -1;
}

void Collider::SwapPoints(std::size_t a, std::size_t b) {
	if (a < m.points.size() && b < m.points.size()) {
		std::swap(m.points[a], m.points[b]);
	}
	CalculateLines();
}
