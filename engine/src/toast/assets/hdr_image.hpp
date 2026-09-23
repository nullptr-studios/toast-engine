/**
 * @file hdr_image.hpp
 * @author dario
 * @date 07/08/2026
 *
 * @brief Decoding for high-dynamic-range images, used for imported environment maps
 */

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace assets {

/**
 * @brief A decoded floating-point image, always four channels
 *
 * Four rather than three because Vulkan has no widely-supported three-channel float format - `R32G32B32Sfloat`
 * is optional and absent on plenty of desktop parts, while `R32G32B32A32Sfloat` is universal. The alpha is
 * padding and always 1
 */
struct HdrImage {
	uint32_t width = 0;
	uint32_t height = 0;
	/// Row-major, width * height * 4 floats, linear radiance rather than display values
	std::vector<float> pixels;

	[[nodiscard]]
	auto valid() const noexcept -> bool {
		return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * height * 4;
	}
};

/**
 * @brief Decodes a Radiance `.hdr` image held in memory
 *
 * Radiance is the format essentially every free environment library ships, and it decodes to linear radiance
 * directly - no transfer function to undo and no exposure baked in, which is exactly what an environment map
 * has to be. It is read here rather than converted to KTX2 at import: `toktx` only ingests PNG, so a KTX2
 * route would need the library driven directly, and a format this simple does not earn that
 *
 * @param bytes Whole file contents
 * @returns a decoded image, or one where valid() is false if @p bytes is not a readable HDR
 */
[[nodiscard]]
auto decodeHdr(std::span<const uint8_t> bytes) -> HdrImage;

}
