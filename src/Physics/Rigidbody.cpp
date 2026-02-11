#include "Toast/Physics/Rigidbody.hpp"

#include "PhysicsSystem.hpp"
#include "Toast/GlmJson.hpp"
#include "Toast/Objects/Actor.hpp"
#include "Toast/Physics/Raycast.hpp"
#include "Toast/Profiler.hpp"
#include "Toast/Renderer/DebugDrawLayer.hpp"

#ifdef TOAST_EDITOR
#include <imgui.h>
#endif

namespace physics {

void Rigidbody::Init() {
	PROFILE_ZONE;
	PhysicsSystem::AddRigidbody(this);

	// Initialize interpolation positions from the current transform
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	glm::vec3 worldPos = transform->worldPosition();
	m_currentPosition = glm::dvec2(worldPos.x, worldPos.y);
	m_previousPosition = m_currentPosition;
	m_lastKnownTransformPos = worldPos;
	m_hasValidPreviousPosition = true;
}

void Rigidbody::Destroy() {
	PhysicsSystem::RemoveRigidbody(this);
}

#ifdef TOAST_EDITOR
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
	ImGui::DragScalarN("Drag", ImGuiDataType_Float, &drag.x, 2);

	ImGui::Spacing();
	ImGui::DragScalarN("Minimum Velocity", ImGuiDataType_Double, &minimumVelocity.x, 2);

	ImGui::Spacing();
	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Draw", &debug.show);
	ImGui::Checkbox("Draw manifolds", &debug.showManifolds);
	ImGui::ColorEdit4("Default color", &debug.defaultColor.r);
	ImGui::ColorEdit4("Colliding color", &debug.collidingColor.r);

	ImGui::Spacing();
	ImGui::SeparatorText("Collider Flags");
	unsigned int cur = static_cast<unsigned int>(flags);
	bool default_flag = (cur & static_cast<unsigned int>(ColliderFlags::Default)) != 0;
	bool ground_flag = (cur & static_cast<unsigned int>(ColliderFlags::Ground)) != 0;
	bool player_flag = (cur & static_cast<unsigned int>(ColliderFlags::Player)) != 0;
	bool enemy_flag = (cur & static_cast<unsigned int>(ColliderFlags::Enemy)) != 0;

	if (ImGui::Checkbox("Default", &default_flag)) {
		if (default_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Default);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Default);
		}
	}
	if (ImGui::Checkbox("Ground", &ground_flag)) {
		if (ground_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Ground);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Ground);
		}
	}
	if (ImGui::Checkbox("Enemy", &enemy_flag)) {
		if (enemy_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Enemy);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Enemy);
		}
	}
	if (ImGui::Checkbox("Player", &player_flag)) {
		if (player_flag) {
			cur |= static_cast<unsigned int>(ColliderFlags::Player);
		} else {
			cur &= ~static_cast<unsigned int>(ColliderFlags::Player);
		}
	}

	flags = static_cast<ColliderFlags>(cur);

	ImGui::Spacing();

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
}
#endif

json_t Rigidbody::Save() const {
	json_t j = Component::Save();

	j["radius"] = radius;
	j["mass"] = mass;
	j["friction"] = friction;
	j["gravityScale"] = gravityScale;
	j["drag"] = drag;
	j["restitution"] = restitution;
	j["restitutionThreshold"] = restitutionThreshold;
	j["minimumVelocity"] = minimumVelocity;

	j["debug.show"] = debug.show;
	j["debug.defaultColor"] = debug.defaultColor;
	j["debug.collidingColor"] = debug.collidingColor;
	j["flags"] = static_cast<unsigned int>(flags);
	return j;
}

void Rigidbody::Load(json_t j, bool propagate) {
	PROFILE_ZONE_C(0x00FFFF);  // Cyan for deserialization
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
	if (j.contains("drag")) {
		drag = j["drag"];
	}
	if (j.contains("restitution")) {
		restitution = j["restitution"];
	}
	if (j.contains("restitutionThreshold")) {
		restitutionThreshold = j["restitutionThreshold"];
	}
	if (j.contains("minumumVelocity")) {
		minimumVelocity = j["minimumVelocity"];
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
	if (j.contains("flags")) {
		flags = static_cast<ColliderFlags>(j["flags"].get<unsigned int>());
	}

	Component::Load(j, propagate);
}

glm::dvec2 Rigidbody::GetPosition() const {
	return m_currentPosition;
}

void Rigidbody::SetPosition(glm::dvec2 pos) {
	m_currentPosition = pos;

	// Initialize previous position on first call to avoid teleporting
	if (!m_hasValidPreviousPosition) {
		m_previousPosition = pos;
		m_hasValidPreviousPosition = true;
	}

	//@Note: Visual transform is updated by PhysicsSystem::UpdateVisualInterpolation()
}

glm::dvec2 Rigidbody::GetInterpolatedPosition() const {
	if (!m_hasValidPreviousPosition) {
		return m_currentPosition;
	}
	return glm::mix(m_previousPosition, m_currentPosition, s_interpolationAlpha);
}

void Rigidbody::StorePreviousPosition() {
	m_previousPosition = m_currentPosition;
	m_hasValidPreviousPosition = true;
}

void Rigidbody::UpdateVisualTransform() {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	glm::vec3 currentTransformPos = transform->worldPosition();
	float z = currentTransformPos.z;

	// Check if transform was manually modified
	const double epsilon = 1e-6;
	bool transformManuallyChanged =
	    std::abs(currentTransformPos.x - m_lastKnownTransformPos.x) > epsilon || std::abs(currentTransformPos.y - m_lastKnownTransformPos.y) > epsilon;

	if (transformManuallyChanged) {
		SyncFromTransform();
	} else {
		// interp
		glm::dvec2 interpPos = GetInterpolatedPosition();
		glm::vec3 newPos = { interpPos.x, interpPos.y, z };
		transform->worldPosition(newPos);
		m_lastKnownTransformPos = newPos;
	}
}

void Rigidbody::SyncFromTransform() {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	glm::vec3 worldPos = transform->worldPosition();

	// Update rigidbody position to match transform
	m_currentPosition = glm::dvec2(worldPos.x, worldPos.y);
	m_previousPosition = m_currentPosition;
	m_lastKnownTransformPos = worldPos;
	m_hasValidPreviousPosition = true;

	// velocity = { 0.0, 0.0 };
}

void Rigidbody::UpdateInterpolationAlpha(double alpha) {
	s_interpolationAlpha = glm::clamp(alpha, 0.0, 1.0);
}

double Rigidbody::GetInterpolationAlpha() {
	return s_interpolationAlpha;
}

void Rigidbody::AddForce(glm::dvec2 force) {
	forcesMutex.lock();
	forces.emplace_back(force);
	forcesMutex.unlock();
}

}
