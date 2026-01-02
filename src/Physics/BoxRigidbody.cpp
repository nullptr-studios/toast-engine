#include "Engine/Physics/BoxRigidbody.hpp"

#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"

#include <imgui.h>

namespace physics {

void BoxRigidbody::Init() {
	PhysicsSystem::AddBox(this);
}

void BoxRigidbody::Destroy() {
	PhysicsSystem::RemoveBox(this);
}

void BoxRigidbody::Inspector() {
	ImGui::SeparatorText("Properties");

	ImGui::DragScalarN("Size", ImGuiDataType_Double, &size.x, 2);
	ImGui::DragScalarN("Offset", ImGuiDataType_Double, &offset.x, 2);
	ImGui::DragScalar("Rotation", ImGuiDataType_Double, &rotation);
	ImGui::DragScalar("Mass", ImGuiDataType_Double, &mass);
	ImGui::DragScalar("Friction", ImGuiDataType_Double, &friction);

	ImGui::Spacing();
	ImGui::SeparatorText("Simulation");

	ImGui::DragScalarN("Gravity Scale", ImGuiDataType_Float, &gravityScale.x, 2);
	ImGui::DragScalar("Restitution", ImGuiDataType_Double, &restitution);
	ImGui::DragScalar("Restitution Threshold", ImGuiDataType_Double, &restitutionThreshold);
	ImGui::DragScalar("Linear Drag", ImGuiDataType_Double, &linearDrag);
	ImGui::DragScalar("Angular Drag", ImGuiDataType_Double, &angularDrag);
	ImGui::Checkbox("Lock rotation", &disableAngular);

	ImGui::Spacing();
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Draw", &debug.show);
	ImGui::Checkbox("Draw manifolds", &debug.showManifolds);
	ImGui::ColorEdit4("Default color", &debug.defaultColor.r);
	ImGui::ColorEdit4("Colliding color", &debug.collidingColor.r);

	ImGui::Spacing();

	if (ImGui::Button("Reset")) {
		SetPosition({ 0.0, 0.0 });
		velocity = { 0.0, 0.0 };
	}

	ImGui::DragScalarN("Velocity", ImGuiDataType_Double, &velocity.x, 2);
	ImGui::DragScalar("Angular Velocity", ImGuiDataType_Double, &angularVelocity);

	ImGui::Spacing();

	if (ImGui::Button("Add force")) {
		AddForce(debug.addForce);
	}
	ImGui::SameLine();
	ImGui::DragFloat2("Debug force", &debug.addForce.x);
}

void BoxRigidbody::EditorTick() {
	if (!debug.show) {
		return;
	}

	glm::dvec2 position = GetPosition();
	double rotation = GetRotation();
	glm::dvec2 scale = size;

	std::vector<glm::vec2> points = GetPoints();

	// Half-extents for centering
	glm::dvec2 h = scale * 0.5;
	double cos_r = std::cos(rotation);
	double sin_r = std::sin(rotation);

	// Define local corners relative to center
	std::array<glm::dvec2, 4> corners = {
		glm::dvec2 { -h.x, -h.y },
     glm::dvec2 {  h.x, -h.y },
     glm::dvec2 {  h.x,  h.y },
     glm::dvec2 { -h.x,  h.y }
	};

	for (int i = 0; i < 4; i++) {
		// Rotation -> Position
		points[i].x = (float)((corners[i].x * cos_r) - (corners[i].y * sin_r) + position.x);
		points[i].y = (float)((corners[i].x * sin_r) + (corners[i].y * cos_r) + position.y);
	}

	renderer::DebugPoly(points, debug.defaultColor);
}

auto BoxRigidbody::GetPosition() const -> glm::dvec2 {
	return static_cast<toast::Actor*>(parent())->transform()->worldPosition();
}

void BoxRigidbody::SetPosition(glm::dvec2 position) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float z = transform->worldPosition().z;
	transform->worldPosition({ position.x, position.y, z });
}

auto BoxRigidbody::GetRotation() const -> double {
	return static_cast<toast::Actor*>(parent())->transform()->worldRotationRadians().z;
}

void BoxRigidbody::SetRotation(double rotation) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float x = transform->worldRotationRadians().x;
	float y = transform->worldRotationRadians().y;
	transform->worldRotationRadians({ x, y, rotation });
}

auto BoxRigidbody::GetPoints() const -> std::vector<glm::vec2> {
	glm::dvec2 position = GetPosition();
	double rotation = GetRotation();
	glm::dvec2 scale = size;

	std::vector<glm::vec2> points(4);

	// Half-extents for centering
	glm::dvec2 h = scale * 0.5;
	double cos_r = std::cos(rotation);
	double sin_r = std::sin(rotation);

	// Define local corners relative to center
	std::array<glm::dvec2, 4> corners = {
		glm::dvec2 { -h.x, -h.y },
     glm::dvec2 {  h.x, -h.y },
     glm::dvec2 {  h.x,  h.y },
     glm::dvec2 { -h.x,  h.y }
	};

	for (int i = 0; i < 4; i++) {
		// Rotation -> Position
		points[i].x = (float)((corners[i].x * cos_r) - (corners[i].y * sin_r) + position.x);
		points[i].y = (float)((corners[i].x * sin_r) + (corners[i].y * cos_r) + position.y);
	}

	return points;
}

auto BoxRigidbody::GetEdges() const -> std::vector<glm::vec2> {
	std::vector<glm::vec2> points = GetPoints();
	std::vector<glm::vec2> normals;
	normals.reserve(points.size());

	for (int i = 0; i < points.size(); i++) {
		const glm::vec2& p1 = points[i];
		const glm::vec2& p2 = points[(i + 1) % points.size()];

		// compute edge direction
		glm::vec2 edge = p2 - p1;

		// compute perpendicular vector
		glm::vec2 normal = glm::normalize(glm::vec2 { -edge.y, edge.x });

		normals.emplace_back(normal);
	}

	return normals;
}

}
