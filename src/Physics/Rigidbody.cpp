#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"
#include "imgui.h"

using namespace physics;

glm::vec2 Rigidbody::GetPosition() {
	return static_cast<toast::Actor*>(parent())->transform()->position();
}

void Rigidbody::SetPosition(glm::vec2 position) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	transform->position({position.x, position.y, transform->position().z});
}

void Rigidbody::Init() {
	toast::Component::Init();

	PhysicsSystem::AddRigidbody(this);
}

void Rigidbody::Inspector() {
	if (ImGui::Button("Reset position")) {
		static_cast<toast::Actor*>(parent())->transform()->position({ 0.0f, 0.0f, 0.0f });
	}

	ImGui::DragFloat2("Velocity", &velocity.x);

	ImGui::DragFloat2("Bounds", &bounds.x);
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
