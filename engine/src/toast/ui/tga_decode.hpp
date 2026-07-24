/**
 * @file tga_decode.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Minimal TGA decoder for UI images
 */

#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ui {

struct TgaImage {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> pixels;
};

/**
 * @brief Decodes an uncompressed or RLE true-color TGA (24/32-bit) into RGBA8
 *
 * Grayscale and color-mapped images are not supported
 */
auto decodeTga(std::span<const uint8_t> data) -> std::optional<TgaImage>;

}
