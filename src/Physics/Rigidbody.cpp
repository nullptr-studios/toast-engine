#include "Toast/Physics/Rigidbody.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Physics/Raycast.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

#include <imgui.h>

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

	ImGui::DragScalarN("Gravity Scale", ImGuiDataType_Float, &gravityScale.x, 2);
	ImGui::DragScalar("Restitution", ImGuiDataType_Double, &restitution);
	ImGui::DragScalar("Restitution Threshold", ImGuiDataType_Double, &restitutionThreshold);
	ImGui::DragScalar("Linear Drag", ImGuiDataType_Double, &linearDrag);

	ImGui::Spacing();
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Draw", &debug.show);
	ImGui::Checkbox("Draw manifolds", &debug.showManifolds);
	ImGui::ColorEdit4("Default color", &debug.defaultColor.r);
	ImGui::ColorEdit4("Colliding color", &debug.collidingColor.r);

	ImGui::Spacing();
	ImGui::Checkbox("RayCast Test", &debug.rayTest);

	if (ImGui::Button("Reset")) {
		SetPosition({ 0.0, 0.0 });
		velocity = { 0.0, 0.0 };
	}

	ImGui::DragScalarN("Velocity", ImGuiDataType_Double, &velocity.x, 2);

	ImGui::Spacing();

	if (ImGui::Button("Add force")) {
		AddForce(debug.addForce);
	}
	ImGui::SameLine();
	ImGui::DragFloat2("Debug force", &debug.addForce.x);
}

void Rigidbody::EditorTick() {
	if (!debug.show) {
		return;
	}
	renderer::DebugCircle(GetPosition(), radius, debug.defaultColor);

	if (debug.rayTest) {
		RayCast(GetPosition(), glm::vec2(1.0f,0.0f));
		RayCast(GetPosition(), glm::vec2(0.5f, 1.73f));
		RayCast(GetPosition(), glm::vec2(1.73f,0.5f));
		RayCast(GetPosition(), glm::vec2(0,1));
		RayCast(GetPosition(), glm::vec2(-1.73f,0.5f));
		RayCast(GetPosition(), glm::vec2(-0.5,1.73f));
		RayCast(GetPosition(), glm::vec2(-1.0f,0.0f));
		RayCast(GetPosition(), glm::vec2(-0.5,-1.73f));
		RayCast(GetPosition(), glm::vec2(-1.73f,-0.5f));
		RayCast(GetPosition(), glm::vec2(0.0f,-1.0f));
		RayCast(GetPosition(), glm::vec2(0.5f,-1.73f));
		RayCast(GetPosition(), glm::vec2(1.73f,-0.5f));
	}
}

json_t Rigidbody::Save() const {
	json_t j = Component::Save();

	j["radius"] = radius;
	j["mass"] = mass;
	j["friction"] = friction;
	j["gravityScale"] = gravityScale;
	j["linearDrag"] = linearDrag;
	j["restitution"] = restitution;
	j["restitutionThreshold"] = restitutionThreshold;

	j["debug.show"] = debug.show;
	j["debug.defaultColor"] = debug.defaultColor;
	j["debug.collidingColor"] = debug.collidingColor;
	j["debug.rayTest"] = debug.rayTest;
	return j;
}

void Rigidbody::Load(json_t j, bool propagate) {
	if (j.contains("radius")) {
		radius = j["radius"];
	}
	if (j.contains("mass")) {
		mass = j["mass"];
	}
	if (j.contains("friction")) {
		friction = j["friction"];
	}
	if (j.contains("gravityScale")) {
		gravityScale = j["gravityScale"];
	}
	if (j.contains("linearDrag")) {
		linearDrag = j["linearDrag"];
	}
	if (j.contains("restitution")) {
		restitution = j["restitution"];
	}
	if (j.contains("restitutionThreshold")) {
		restitutionThreshold = j["restitutionThreshold"];
	}

	if (j.contains("debug.show")) {
		debug.show = j["debug.show"];
	}
	if (j.contains("debug.defaultColor")) {
		debug.defaultColor = j["debug.defaultColor"];
	}
	if (j.contains("debug.collidingColor")) {
		debug.collidingColor = j["debug.collidingColor"];
	}
	if (j.contains("debug.rayTest")) {
		debug.rayTest = j["debug.rayTest"];
	}

	Component::Load(j, propagate);
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
