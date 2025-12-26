#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"
#include "imgui.h"

using namespace physics;

auto Rigidbody::data() const -> RigidbodyData {
	return { .position = static_cast<toast::Actor*>(parent())->transform()->position(), .velocity = velocity, .radius = radius };
}

void Rigidbody::data(const RigidbodyData& data) {
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
}

void Rigidbody::UpdatePosition() {
	// auto* transform = static_cast<toast::Actor*>(parent())->transform();
	// glm::vec2 position = { transform->position().x, transform->position().y };
	//
	// if (position.y + radius > bounds.y) {
	// 	position.y = bounds.y - radius;
	// 	velocity.y = -velocity.y;
	// } else if (position.y - radius < -bounds.y) {
	// 	position.y = -bounds.y + radius;
	// 	velocity.y = -velocity.y;
	// }
	//
	// if (position.x + radius > bounds.x) {
	// 	position.x = bounds.x - radius;
	// 	velocity.x = -velocity.x;
	// } else if (position.x - radius < -bounds.x) {
	// 	position.x = -bounds.x + radius;
	// 	velocity.x = -velocity.x;
	// }
	//
	// position.x += velocity.x * Time::fixed_delta();
	// position.y += velocity.y * Time::fixed_delta();
	//
	// transform->position({ position.x, position.y, transform->position().z });
}
