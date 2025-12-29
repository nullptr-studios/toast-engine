#include "Engine/Physics/Rigidbody.hpp"

#include "Engine/Toast/Objects/Actor.hpp"
#include "PhysicsSystem.hpp"

using namespace physics;

void Rigidbody::Init() {
	PhysicsSystem::AddRigidbody(this);
}

void Rigidbody::Destroy() {
	PhysicsSystem::RemoveRigidbody(this);
}

glm::dvec2 Rigidbody::GetPosition() {
	return static_cast<toast::Actor*>(parent())->transform()->worldPosition();
}

void Rigidbody::SetPosition(glm::dvec2 pos) {
	auto* transform = static_cast<toast::Actor*>(parent())->transform();
	float z = transform->worldPosition().z;
	transform->worldPosition({ pos.x, pos.y, z });
}
