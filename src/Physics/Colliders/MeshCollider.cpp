#include "../inc/Engine/Physics/Colliders/MeshCollider.hpp"

#include "../src/Physics/PhysicsSystem.hpp"

namespace physics {

void MeshCollider::Init() {
	throw std::logic_error("Not implemented yet!");
}

void MeshCollider::Destroy() {
	RemoveCollider(this);
}

}
