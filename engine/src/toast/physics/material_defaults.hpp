/**
 * @file material_defaults.hpp
 * @author Xein
 * @date 12 Oct 2026
 * @brief The values a material has when nothing sets them
 */

#pragma once

#include "fracture_pattern.hpp"
#include "hardness.hpp"

#include <cstdint>

namespace physics::material_defaults {

inline constexpr float k_restitution = 0.1f;
inline constexpr float k_static_friction = 0.6f;
inline constexpr float k_dynamic_friction = 0.5f;

/// A ball rolling on a hard floor loses a few percent of its speed per metre
inline constexpr float k_rolling_friction = 0.01f;
inline constexpr float k_spin_friction = 0.005f;

/// between wood at 600 and stone at 2600
inline constexpr float k_density = 2000.0f;
inline constexpr float k_toughness = 100.0f;
inline constexpr Hardness k_hardness = Hardness::medium;
inline constexpr FracturePattern k_fracture_pattern = FracturePattern::brittle;

/// weak bricks
inline constexpr float k_structural_strength = 1.0e6f;

inline constexpr float k_debris_share = 0.2f;
inline constexpr float k_fragment_share = 0.5f;
inline constexpr float k_fragment_threshold = 0.3f;
inline constexpr uint32_t k_fragment_max_parts = 4;
inline constexpr float k_shatter_threshold = 0.1f;
inline constexpr float k_shatter_cell_size = 0.5f;
inline constexpr float k_shatter_crack_cost = 1.0f;
inline constexpr float k_shatter_release = 0.3f;
inline constexpr float k_shatter_stretch = 3.0f;
inline constexpr float k_shatter_crumble = 0.5f;
inline constexpr float k_edge_noise = 0.25f;
inline constexpr float k_edge_lobes = 0.35f;
inline constexpr float k_edge_noise_scale = 4.0f;

}
