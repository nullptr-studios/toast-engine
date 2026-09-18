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

}
