/// @file Physics.hpp
/// @author Xein
/// @date 28 Feb 2026

#pragma once

namespace physics {
class Rigidbody;

auto GetAllRigidbodies() -> std::list<Rigidbody*>&;
auto Gravity() -> float;
};
