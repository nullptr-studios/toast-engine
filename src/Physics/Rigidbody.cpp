#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Renderer/DebugDrawLayer.hpp"
#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"
#include "imgui.h"

using namespace physics;

void Rigidbody::Init() {
	PhysicsSystem::AddRigidbody(this);
}

void Rigidbody::Destroy() {
	PhysicsSystem::RemoveRigidbody(this);
}

void Rigidbody::Inspector() {
	ImGui::SeparatorText("Properties");

	ImGui::DragScalar("Radius", ImGuiDataType_Double, &radius);
	ImGui::DragScalar("Mass", ImGuiDataType_Double, &mass);
	ImGui::DragScalar("Friction", ImGuiDataType_Double, &friction);

	ImGui::Spacing();
	ImGui::SeparatorText("Simulation");

	ImGui::DragScalarN("Gravity Scale", ImGuiDataType_Double, &gravityScale.x, 2);
	ImGui::DragScalar("Restitution", ImGuiDataType_Double, &restitution);
	ImGui::DragScalar("Restitution Threshold", ImGuiDataType_Double, &restitutionThreshold);
	ImGui::DragScalar("Linear Drag", ImGuiDataType_Double, &linearDrag);

	ImGui::Spacing();
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Draw", &debug);
	ImGui::ColorEdit4("Default color", &defaultColor.r);
	ImGui::ColorEdit4("Colliding color", &collidingColor.r);

	ImGui::Spacing();

	if (ImGui::Button("Reset")) {
		SetPosition({ 0.0, 0.0 });
		velocity = { 0.0, 0.0 };
	}

	ImGui::DragScalarN("Velocity", ImGuiDataType_Double, &velocity.x, 2);

	ImGui::Spacing();

	if (ImGui::Button("Add force")) {
		AddForce(addForceDebug);
	}
	ImGui::SameLine();
	ImGui::DragFloat2("Debug force", &addForceDebug.x);
}

void Rigidbody::EditorTick() {
	if (!debug) {
		return;
	}
	renderer::DebugCircle(GetPosition(), radius, defaultColor);
}

glm::dvec2 Rigidbody::GetPosition() const {
	return static_cast<toast::Actor*>(parent())->transform()->worldPosition();
}

void Rigidbody::SetPosition(glm::dvec2 pos) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float z = transform->worldPosition().z;
	transform->worldPosition({ pos.x, pos.y, z });
}

void Rigidbody::AddForce(glm::dvec2 force) {
	forces.emplace_back(force);
}
