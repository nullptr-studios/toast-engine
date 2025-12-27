#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"
#include "imgui.h"

using namespace physics;

auto Rigidbody::data() const -> RigidbodyData {
	return { .position = static_cast<toast::Actor*>(parent())->transform()->worldPosition(), .velocity = velocity, .radius = radius };
}

void Rigidbody::data(const RigidbodyData& data) {
	if (!simulate) {
		return;
	}
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	transform->position({ data.position.x, data.position.y, transform->position().z });
	velocity = data.velocity;
	radius = data.radius;
}

void Rigidbody::Init() {
	toast::Component::Init();

	PhysicsSystem::AddRigidbody(this);
}

void Rigidbody::Destroy() {
	PhysicsSystem::RemoveRigidbody(this);
}

void Rigidbody::Inspector() {
	if (ImGui::Button("Reset position")) {
		static_cast<toast::Actor*>(parent())->transform()->position({ 0.0f, 0.0f, 0.0f });
	}

	ImGui::DragFloat2("Velocity", &velocity.x);
	ImGui::DragFloat("Radius", &radius);
	ImGui::Checkbox("Enable simulation", &simulate);
}
