/// @file BoxDynamics.hpp
/// @author Xein
/// @date 1 Jan 2025

#pragma once
#include "Manifold.hpp"

namespace physics {

class BoxRigidbody;
class ConvexCollider;
class Trigger;
struct Line;

auto LineLineCollision(const Line& a, const Line& b) -> std::optional<glm::dvec2>;
void BoxKinematics(BoxRigidbody* rb);
void BoxIntegration(BoxRigidbody* rb);
void BoxResetVelocity(BoxRigidbody* rb);
auto BoxBoxCollision(BoxRigidbody* rb1, BoxRigidbody* rb2) -> std::optional<BoxManifold>;
void BoxBoxResolution(BoxRigidbody* rb1, BoxRigidbody* rb2, BoxManifold manifold);
auto BoxMeshCollision(BoxRigidbody* rb, ConvexCollider* c) -> std::optional<BoxManifold>;
void BoxMeshResolution(BoxRigidbody* rb, ConvexCollider* c, BoxManifold manifold);

}
