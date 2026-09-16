#include "texture.h"    // ffi

#include <cstring>
#include <fstream>
#include <ktx.h>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <vector>

namespace {

// The only two formats toast_ktx2_decode_thumbnail knows how to read pixels back out of - either the format
// ktxTexture2_TranscodeBasis(..., KTX_TTF_RGBA32, ...) always produces, or a source file that was already
// stored uncompressed in one of these. Anything else (BC7, ASTC, ...) would need a block decompressor this
// editor-side utility doesn't have.
constexpr uint32_t k_vk_format_r8g8b8a8_unorm = 37;
constexpr uint32_t k_vk_format_r8g8b8a8_srgb = 43;

}    // namespace

extern "C" {

auto toast_ktx2_decode_thumbnail(const char* path, uint8_t* dst, uint32_t thumb_size) noexcept -> int {
	ZoneScoped;
	try {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail: could not open '{}'", path);
			return 0;
		}
		const auto size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> data(static_cast<size_t>(size));
		file.read(reinterpret_cast<char*>(data.data()), size);

		ktxTexture2* texture = nullptr;
		auto result = ktxTexture2_CreateFromMemory(data.data(), data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
		if (result != KTX_SUCCESS) {
			TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail: failed to open '{}'", path);
			return 0;
		}

		if (ktxTexture2_NeedsTranscoding(texture)) {
			result = ktxTexture2_TranscodeBasis(texture, KTX_TTF_RGBA32, 0);
			if (result != KTX_SUCCESS) {
				TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail: failed to transcode '{}'", path);
				ktxTexture_Destroy(ktxTexture(texture));
				return 0;
			}
		}

		if (texture->vkFormat != k_vk_format_r8g8b8a8_unorm && texture->vkFormat != k_vk_format_r8g8b8a8_srgb) {
			TOAST_ERROR(
			    "AssetManager",
			    "toast_ktx2_decode_thumbnail: '{}' is not RGBA8 after transcoding (vkFormat={})",
			    path,
			    texture->vkFormat
			);
			ktxTexture_Destroy(ktxTexture(texture));
			return 0;
		}

		ktx_size_t offset = 0;
		if (ktxTexture2_GetImageOffset(texture, 0, 0, 0, &offset) != KTX_SUCCESS) {
			TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail: could not locate base image in '{}'", path);
			ktxTexture_Destroy(ktxTexture(texture));
			return 0;
		}

		const uint32_t width = texture->baseWidth;
		const uint32_t height = texture->baseHeight;
		const uint8_t* src_pixels = ktxTexture_GetData(ktxTexture(texture)) + offset;

		// Nearest-neighbor downsample - good enough for a small preview icon and avoids pulling in a
		// full image-resampling dependency just for this
		for (uint32_t y = 0; y < thumb_size; ++y) {
			const uint32_t src_y = (y * height) / thumb_size;
			for (uint32_t x = 0; x < thumb_size; ++x) {
				const uint32_t src_x = (x * width) / thumb_size;
				std::memcpy(&dst[((y * thumb_size) + x) * 4], &src_pixels[((src_y * width) + src_x) * 4], 4);
			}
		}

		ktxTexture_Destroy(ktxTexture(texture));
		return 1;
	} catch (const std::exception& e) {
		TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail failed: {}", e.what());
		return 0;
	} catch (...) {
		TOAST_ERROR("AssetManager", "toast_ktx2_decode_thumbnail failed with an unrecognized exception");
		return 0;
	}
}
}
