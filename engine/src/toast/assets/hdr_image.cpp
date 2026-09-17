/**
 * @file hdr_image.cpp
 * @author dario
 * @date 07/08/2026
 *
 * @note Carries STB_IMAGE_IMPLEMENTATION, so it is excluded from the unity build and the PCH in
 *       engine/CMakeLists.txt - same treatment as vma.cpp. An implementation macro landing in a shared
 *       translation unit defines the whole library into whatever else happened to be batched with it
 */

#include "hdr_image.hpp"

// Only the HDR decoder is wanted here; the rest of stb_image is a large amount of code for formats the
// texture pipeline already handles through KTX2
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include <stb_image.h>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>

namespace assets {

auto decodeHdr(std::span<const uint8_t> bytes) -> HdrImage {
	ZoneScoped;
	HdrImage image;
	if (bytes.empty()) {
		return image;
	}

	int width = 0;
	int height = 0;
	int channels = 0;

	// Four channels requested explicitly - see HdrImage on why three is not an option
	float* decoded = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);

	if (decoded == nullptr) {
		TOAST_ERROR("Assets", "HDR decode failed: {}", stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown");
		return image;
	}

	if (width <= 0 || height <= 0) {
		stbi_image_free(decoded);
		return image;
	}

	image.width = static_cast<uint32_t>(width);
	image.height = static_cast<uint32_t>(height);

	const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
	image.pixels.assign(decoded, decoded + count);
	stbi_image_free(decoded);

	return image;
}

}
