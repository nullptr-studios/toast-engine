/// @file Physics.hpp
/// @author Xein
/// @date 28 Feb 2026

#pragma once
#include "GravityType.hpp"

namespace physics {
class Rigidbody;

auto GetAllRigidbodies() -> std::list<Rigidbody*>&;
auto Gravity() -> float;

void SetGravityType(GravityType type);
void SetGravityPoint(glm::dvec2 pos);
void SetGravityPointScale(double scale);

};
