#include "../inc/Engine/Physics/RigidbodyComponent.hpp"

#include "../src/Physics/PhysicsSystem.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Physics/Colliders/BoxCollider.hpp"
#include "Engine/Physics/Colliders/CircleCollider.hpp"
#include "Engine/Physics/Colliders/MeshCollider.hpp"
#include "Engine/Toast/BadObjectException.hpp"
#include "Engine/Toast/Objects/Actor.hpp"
#ifdef TOAST_EDITOR
#include "imgui.h"
#endif

#include <Engine/Core/GlmJson.hpp>

namespace physics {
using namespace glm;

void RigidbodyComponent::Init() {
	Component::Init();
}

void RigidbodyComponent::Begin() {
	Component::Begin();

	AddRigidbody(this);

	// Get collider from parent
	if (parent()->base_type() != toast::ActorT) {
		throw toast::BadObject(parent(), "Rigidbody can only be placed on an Actor");
	}

	auto* actor = static_cast<toast::Actor*>(parent());
	m_transform = actor->transform();

	m_collider = actor->children.Get<ICollider>();
	if (m_collider == nullptr) {
		throw toast::BadObject(parent(), "Rigidbody requires a collider in parent");
	}
	m_collider->m_rigidbody = this;

	// reset physics values
	m_velocity = { 0.0f, 0.0f };
	m_alexeyVelocity = { 0.0f, 0.0f };
	m_acceleration = { 0.0f, 0.0f };
	m_angularVelocity = 0.0f;
	m_angularAcceleration = 0.0f;
}

void RigidbodyComponent::PhysTick() {
	Component::PhysTick();

#ifdef TOAST_EDITOR
	if (!has_run_begin()) {
		TOAST_WARN("Tried to simulate physics but Rigidbody in \"{0}\" hasn't run the begin", parent()->name());
		return;
	}
#endif

	if (rigidbodyType == Dynamic) {
		float dt = static_cast<float>(Time::fixed_delta());
		vec2 gravity = PhysicsSystem::gravity() * gravityScale;
		vec2 accel = m_acceleration + gravity;
		m_velocity += accel * dt;
		m_velocity.x *= 1 - (drag.x * dt);
		m_velocity.y *= 1 - (drag.y * dt);
		m_alexeyVelocity.x *= 1 - (drag.x * dt);
		m_alexeyVelocity.y *= 1 - (drag.y * dt);
		m_velocity = min(m_velocity, terminalVelocity);
		vec2 velocity_composite = m_velocity + m_alexeyVelocity;
		m_transform->position(m_transform->position() + vec3 { velocity_composite.x, velocity_composite.y, 0.0f } * dt);
		m_acceleration = { 0.0f, 0.0f };

		m_angularVelocity += m_angularAcceleration * dt;
		m_angularVelocity *= 1 - (angularDrag * dt);
		vec3 rot = m_transform->rotationRadians();
		rot.z += m_angularVelocity * dt;
		m_transform->rotationRadians(rot);
		m_angularAcceleration = 0;

		if (all(lessThan(abs(m_velocity), m_velocityEpsilon))) {
			m_velocity = { 0.0f, 0.0f };
		}
	}
}

void RigidbodyComponent::Destroy() {
	RemoveRigidbody(this);
	Component::Destroy();
}

json_t RigidbodyComponent::Save() const {
	json_t j = Component::Save();

	j["rigidbody_type"] = rigidbodyType;
	j["mass"] = mass;
	j["mass_center"] = centerOfMass;
	j["linear_drag"] = drag;
	j["angular_drag"] = angularDrag;
	j["gravity_scale"] = gravityScale;
	j["velocity_epsilon"] = m_velocityEpsilon;
	j["terminal_velocity"] = terminalVelocity;

	return j;
}

void RigidbodyComponent::Load(json_t j, bool force_create) {
	Component::Load(j, force_create);

	if (j.contains("rigidbody_type")) {
		rigidbodyType = j.at("rigidbody_type");
	}
	if (j.contains("mass")) {
		mass = j.at("mass");
	}
	if (j.contains("mass_center")) {
		centerOfMass = j.at("mass_center");
	}
	if (j.contains("linear_drag")) {
		drag = j.at("linear_drag");
	}
	if (j.contains("angular_drag")) {
		angularDrag = j.at("angular_drag");
	}
	if (j.contains("gravity_scale")) {
		gravityScale = j.at("gravity_scale");
	}
	if (j.contains("velocity_epsilon")) {
		m_velocityEpsilon = j.at("velocity_epsilon");
	}
	if (j.contains("terminal_velocity")) {
		terminalVelocity = j.at("terminal_velocity");
	}
}

#ifdef TOAST_EDITOR
void RigidbodyComponent::Inspector() {
	Component::Inspector();

	const char* preview = TypeNames[static_cast<int>(rigidbodyType)];
	if (ImGui::BeginCombo("Type", preview)) {
		for (int i = 0; i < IM_ARRAYSIZE(TypeNames); ++i) {
			bool selected = (static_cast<int>(rigidbodyType) == i);
			if (ImGui::Selectable(TypeNames[i], selected)) {
				rigidbodyType = static_cast<Type>(i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::DragFloat("Mass", &mass);
	ImGui::DragFloat2("Center of mass", &centerOfMass.x);
	ImGui::Spacing();
	ImGui::DragFloat2("Drag", &drag.x);
	ImGui::DragFloat("Angular Drag", &angularDrag);
	ImGui::Spacing();
	ImGui::DragFloat2("Gravity scale", &gravityScale.x);
	ImGui::DragFloat2("Velocity epsilon", &m_velocityEpsilon.x);
	ImGui::DragFloat2("Terminal velocity", &terminalVelocity.x);

	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Advanced")) {
		ImGui::Indent(20);
		ImGui::DragFloat2("Acceleration", &m_acceleration.x);
		ImGui::DragFloat2("Velocity", &m_velocity.x);
		ImGui::Spacing();
		ImGui::DragFloat("Angular Acceleration", &m_angularAcceleration);
		ImGui::DragFloat("Angular Velocity", &m_angularVelocity);
		ImGui::Spacing();
		if (ImGui::Button("Reset Linear")) {
			m_velocity = { 0.0f, 0.0f };
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Angular")) {
			m_angularVelocity = 0.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset All")) {
			m_velocity = { 0.0f, 0.0f };
			m_angularVelocity = 0.0f;
		}
		ImGui::SameLine();
		if (ImGui::Button("Position to origin")) {
			m_transform->worldPosition({ 0.0f, 0.0f, 0.0f });
		}
		ImGui::Unindent(20);
	}
}
#endif

void RigidbodyComponent::AddForce(vec2 value) {
	if (mass == 0.0f) {
		TOAST_WARN("Mass for {0} was 0, set to 1", parent()->name());
		mass = 1.0f;
	}
	m_acceleration += value / mass;
}

void RigidbodyComponent::AddForce(vec2 value, vec3 position) {
	if (mass == 0.0f) {
		TOAST_WARN("Mass for {0} was 0, set to 1", parent()->name());
		mass = 1.0f;
	}

	m_acceleration += value / mass;

	vec2 r = position - (m_transform->worldPosition() + vec3 { centerOfMass.x, centerOfMass.y, 0.f });
	float torque = r.x * value.y - r.y * value.x;
	AddTorque(torque);
}

static float ComputeMomentOfInertiaFor(const RigidbodyComponent* rb) {
	/// @todo: Prob we should pre-compute this since mass is very rarely going to change
	if (rb->collider()) {
		if (rb->collider()->collider_type() == ICollider::ColliderType::Box) {
			auto* box = static_cast<const BoxCollider*>(rb->collider());
			const vec2 size = box->GetSize();
			return (1.0f / 12.0f) * rb->mass * (size.x * size.x + size.y * size.y);
		}

		if (rb->collider()->collider_type() == ICollider::ColliderType::Circle) {
			auto* circle = dynamic_cast<const CircleCollider*>(rb->collider());
			const float r = circle->GetRadius();
			return 0.5f * rb->mass * r * r;
		}

		// MeshCollider
		// if (auto* mesh = dynamic_cast<const MeshCollider*>(rb->collider())) {
		// 	if (mesh->bounds.has_value()) {
		// 		vec2 size = vec2(mesh->bounds->max.x - mesh->bounds->min.x, mesh->bounds->max.y - mesh->bounds->min.y);
		// 		return (1.0f / 12.0f) * rb->mass * (size.x * size.x + size.y * size.y);
		// 	}
		// }
	}

	return std::max(rb->mass, 1.0f);
}

void RigidbodyComponent::AddTorque(float value) {
	if (mass == 0.0f) {
		TOAST_WARN("Mass for {0} was 0, set to 1", parent()->name());
		mass = 1.0f;
	}

	float I = ComputeMomentOfInertiaFor(this);
	if (I <= 0.0f || !std::isfinite(I)) {
		TOAST_WARN("Invalid moment of inertia for {0}, falling back to mass", parent()->name());
		I = std::max(mass, 1.0f);
	}

	m_angularAcceleration += value / I;
}

void RigidbodyComponent::AddAcceleration(vec2 value) {
	m_acceleration += value;
}

}
