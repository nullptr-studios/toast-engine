/// @file clustered_lighting_constants.hpp
/// @author dario
/// @date 18/07/2026

#pragma once

#include <cstdint>

namespace renderer::clustered_lighting {

inline constexpr uint32_t k_cluster_dim_x = 16;
inline constexpr uint32_t k_cluster_dim_y = 9;
inline constexpr uint32_t k_cluster_dim_z = 24;
inline constexpr uint32_t k_cluster_count = k_cluster_dim_x * k_cluster_dim_y * k_cluster_dim_z;
inline constexpr uint32_t k_max_lights_per_cluster = 128;
inline constexpr uint32_t k_max_lights = 512;

// Slice 0 still starts at the camera near plane
inline constexpr float k_min_cluster_near = 0.1f;
inline constexpr float k_min_cluster_depth_ratio = 4.0f;

}
