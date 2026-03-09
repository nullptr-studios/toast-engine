#include "Toast/Physics/BoxRigidbody.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace physics {

void BoxRigidbody::Init() {
	PhysicsSystem::AddBox(this);
}

void BoxRigidbody::Begin() {
	Component::Begin();
	angularVelocity = 0.0;
	velocity = { 0.0, 0.0 };
	forces.clear();
	torques.clear();
}

void BoxRigidbody::Destroy() {
	PhysicsSystem::RemoveBox(this);
}

#ifdef TOAST_EDITOR
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
#endif

#ifdef TOAST_EDITOR
void BoxRigidbody::EditorTick() {
	if (!debug.show) {
		return;
	}

	const auto& points = GetPoints();
	renderer::DebugPoly(points, debug.defaultColor);
}
#endif

json_t BoxRigidbody::Save() const {
	json_t j = Component::Save();

	j["size"] = size;
	j["offset"] = offset;
	j["rotation"] = rotation;
	j["mass"] = mass;
	j["friction"] = friction;
	j["gravityScale"] = gravityScale;
	j["linearDrag"] = linearDrag;
	j["angularDrag"] = angularDrag;
	j["restitution"] = restitution;
	j["restitutionThreshold"] = restitutionThreshold;

	j["minimumVelocity"] = minimumVelocity;
	j["minimumAngularVelocity"] = minimumAngularVelocity;
	j["disableAngular"] = disableAngular;

	j["debug.show"] = debug.show;
	j["debug.defaultColor"] = debug.defaultColor;
	j["debug.collidingColor"] = debug.collidingColor;
	j["debug.showManifolds"] = debug.showManifolds;

	return j;
}

void BoxRigidbody::Load(json_t j, bool b) {
	if (j.contains("size")) {
		size = j["size"];
	}
	if (j.contains("offset")) {
		offset = j["offset"];
	}
	if (j.contains("rotation")) {
		rotation = j["rotation"];
	}
	if (j.contains("mass")) {
		mass = j["mass"];
	}
	if (j.contains("friction")) {
		friction = j["friction"];
	}

	if (j.contains("linearDrag")) {
		linearDrag = j["linearDrag"];
	}
	if (j.contains("angularDrag")) {
		angularDrag = j["angularDrag"];
	}
	if (j.contains("restitution")) {
		restitution = j["restitution"];
	}
	if (j.contains("restitutionThreshold")) {
		restitutionThreshold = j["restitutionThreshold"];
	}
	if (j.contains("gravityScale")) {
		gravityScale = j["gravityScale"];
	}
	if (j.contains("minimumVelocity")) {
		minimumVelocity = j["minimumVelocity"];
	}
	if (j.contains("minimumAngularVelocity")) {
		minimumAngularVelocity = j["minimumAngularVelocity"];
	}
	if (j.contains("disableAngular")) {
		disableAngular = j["disableAngular"];
	}

	if (j.contains("debug.show")) {
		debug.show = j["debug.show"];
	}
	if (j.contains("debug.showManifolds")) {
		debug.showManifolds = j["debug.showManifolds"];
	}
	if (j.contains("debug.defaultColor")) {
		debug.defaultColor = j["debug.defaultColor"];
	}
	if (j.contains("debug.collidingColor")) {
		debug.collidingColor = j["debug.collidingColor"];
	}

	Component::Load(j, b);
}

auto BoxRigidbody::GetPosition() const -> glm::dvec2 {
	return static_cast<toast::Actor*>(parent())->transform()->worldPosition();
}

void BoxRigidbody::SetPosition(glm::dvec2 position) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float z = transform->worldPosition().z;
	transform->worldPosition({ position.x, position.y, z });
	InvalidateCache();
}

auto BoxRigidbody::GetRotation() const -> double {
	return static_cast<toast::Actor*>(parent())->transform()->worldRotationRadians().z;
}

void BoxRigidbody::SetRotation(double rotation) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float x = transform->worldRotationRadians().x;
	float y = transform->worldRotationRadians().y;
	transform->worldRotationRadians({ x, y, rotation });
	InvalidateCache();
}

void BoxRigidbody::InvalidateCache() const {
	m_cacheDirty = true;
}

void BoxRigidbody::RebuildCache() const {
	glm::dvec2 position = GetPosition();
	double rot = GetRotation();
	glm::dvec2 scale = size;

	// --- Rebuild points ---
	m_cachedPoints.resize(4);

	glm::dvec2 h = scale * 0.5;
	double cos_r = std::cos(rot);
	double sin_r = std::sin(rot);

	std::array<glm::dvec2, 4> corners = {
		glm::dvec2 { -h.x, -h.y },
     glm::dvec2 {  h.x, -h.y },
     glm::dvec2 {  h.x,  h.y },
     glm::dvec2 { -h.x,  h.y }
	};

	for (int i = 0; i < 4; i++) {
		m_cachedPoints[i].x = (float)((corners[i].x * cos_r) - (corners[i].y * sin_r) + position.x);
		m_cachedPoints[i].y = (float)((corners[i].x * sin_r) + (corners[i].y * cos_r) + position.y);
	}

	// --- Rebuild edges ---
	m_cachedEdges.resize(4);
	for (int i = 0; i < 4; i++) {
		const glm::vec2& p1 = m_cachedPoints[i];
		const glm::vec2& p2 = m_cachedPoints[(i + 1) % 4];

		glm::vec2 edge = p2 - p1;
		glm::vec2 normal = glm::normalize(glm::vec2 { -edge.y, edge.x });

		m_cachedEdges[i] = Line {
			.p1 = p1, .p2 = p2, .normal = normal, .tangent = { -normal.y, normal.x },
                 .length = glm::distance(p1, p2)
		};
	}

	m_cachedAABB = renderer::BoundingBox {};
	for (int i = 0; i < 4; i++) {
		m_cachedAABB.expand(glm::vec3(m_cachedPoints[i].x, m_cachedPoints[i].y, -1.0f));
		m_cachedAABB.expand(glm::vec3(m_cachedPoints[i].x, m_cachedPoints[i].y, 1.0f));
	}

	m_cacheDirty = false;
}

auto BoxRigidbody::GetPoints() const -> const std::vector<glm::vec2>& {
	if (m_cacheDirty) {
		RebuildCache();
	}
	return m_cachedPoints;
}

auto BoxRigidbody::GetEdges() const -> const std::vector<Line>& {
	if (m_cacheDirty) {
		RebuildCache();
	}
	return m_cachedEdges;
}

auto BoxRigidbody::GetAABB() const -> const renderer::BoundingBox& {
	if (m_cacheDirty) {
		RebuildCache();
	}
	return m_cachedAABB;
}

void BoxRigidbody::AddForce(glm::dvec2 force) {
	forces.emplace_back(force);
}

void BoxRigidbody::AddTorque(double torque) {
	torques.emplace_back(torque);
}

void BoxRigidbody::AddForce(glm::dvec2 force, glm::dvec2 position) {
	// Get position with [0, 0] as center of mass
	glm::dvec2 rel_pos = position - GetPosition();

	// Decompose into force + torque
	// Linear force always apply
	AddForce(force);

	// torque = cross product position and force
	glm::dmat2 mat = { rel_pos, force };
	AddTorque(glm::determinant(mat));
}

}
