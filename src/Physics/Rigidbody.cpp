#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Core/Time.hpp"
#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"

using namespace physics;

void Rigidbody::Init() {
	toast::Component::Init();

	PhysicsSystem::AddRigidbody(this);
}

void Rigidbody::UpdatePosition() {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	glm::vec2 position = { transform->position().x, transform->position().y };

	position.x += velocity.x * Time::fixed_delta();
	position.y += velocity.y * Time::fixed_delta();

	transform->position({ position.x, position.y, transform->position().z });
}
