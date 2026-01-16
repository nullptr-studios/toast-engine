/// @file RigidbodyDynamics.hpp
/// @author Xein
/// @date 28 Dec 2025

#pragma once
#include "Manifold.hpp"

namespace physics {
struct Line;

class Rigidbody;
class BoxRigidbody;
class ConvexCollider;
class Trigger;

void RbKinematics(Rigidbody* rb);
void RbIntegration(Rigidbody* rb);
void RbResetVelocity(Rigidbody* rb);
auto RbRbCollision(Rigidbody* rb1, Rigidbody* rb2) -> std::optional<Manifold>;
void RbRbResolution(Rigidbody* rb1, Rigidbody* rb2, Manifold manifold);
auto RbBoxCollision(Rigidbody* rb1, BoxRigidbody* rb2) -> std::optional<Manifold>;
void RbBoxResolution(Rigidbody* rb1, BoxRigidbody* rb2, Manifold manifold);
auto RbTriggerCollision(Rigidbody* rb1, Trigger* rb2) -> std::optional<Manifold>;
void RbTriggerResolution(Rigidbody* rb1, Trigger* rb2, Manifold manifold);
auto RbMeshCollision(Rigidbody* rb, ConvexCollider* c) -> std::optional<Manifold>;
void RbMeshResolution(Rigidbody* rb, ConvexCollider* c, Manifold manifold);
std::optional<glm::dvec2> RbRayCollision(Line* ray, Rigidbody* rb);

}
