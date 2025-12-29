/// @file RigidbodyDynamics.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include <glm/glm.hpp>

namespace physics {

struct Manifold{
	glm::dvec2 normal;
	glm::dvec2 contact1;
	glm::dvec2 contact2;
	int contactCount;
	double depth;
};

class Rigidbody;
class BoxRigidbody;
class ConvexCollider;
class Trigger;

void RbKinematics(Rigidbody* rb);
auto RbRbCollision(Rigidbody* rb1, Rigidbody* rb2) -> std::optional<Manifold>;
void RbRbResolution(Rigidbody* rb1, Rigidbody* rb2, Manifold manifold);
auto RbBoxCollision(Rigidbody* rb1, BoxRigidbody* rb2) -> std::optional<Manifold>;
void RbBoxResolution(Rigidbody* rb1, BoxRigidbody* rb2, Manifold manifold);
auto RbTriggerCollision(Rigidbody* rb1, Trigger* rb2) -> std::optional<Manifold>;
void RbTriggerResolution(Rigidbody* rb1, Trigger* rb2, Manifold manifold);
auto RbMeshCollision(Rigidbody* rb, ConvexCollider* c) -> std::optional<Manifold>;
void RbMeshResolution(Rigidbody* rb, ConvexCollider* c, Manifold manifold);

}
