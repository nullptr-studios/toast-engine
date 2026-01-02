/// @file BoxDynamics.hpp
/// @author Xein
/// @date 1 Jan 2025

#pragma once
#include "Manifold.hpp"

namespace physics {

class BoxRigidbody;
class ConvexCollider;
class Trigger;

void BoxKinematics(BoxRigidbody* rb);
void BoxIntegration(BoxRigidbody* rb);
void BoxResetVelocity(BoxRigidbody* rb);
auto BoxBoxCollision(BoxRigidbody* rb1, BoxRigidbody* rb2) -> std::optional<Manifold>;
void BoxBoxResolution(BoxRigidbody* rb1, BoxRigidbody* rb2, Manifold manifold);
auto BoxMeshCollision(BoxRigidbody* rb, ConvexCollider* c) -> std::optional<Manifold>;
void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, Manifold manifold);

}
