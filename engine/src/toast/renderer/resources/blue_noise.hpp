/**
 * @file blue_noise.hpp
 * @author dario
 * @date 18/10/2026
 */

#pragma once

#include <cstdint>
#include <toast/export.hpp>
#include <vector>

namespace renderer::blue_noise {

/// Mirrors lighting.slang kBlueNoiseSize
inline constexpr uint32_t k_size = 64;

/// Void and cluster ranks of one tileable k_size square scaled to 0-255 in row major order
[[nodiscard]]
TOAST_API auto generate(uint32_t seed) -> std::vector<uint8_t>;

/// Four independent channels packed r | g << 8 | b << 16 | a << 24 per texel as lighting.slang gBlueNoise reads them
[[nodiscard]]
TOAST_API auto generatePacked() -> std::vector<uint32_t>;

}
