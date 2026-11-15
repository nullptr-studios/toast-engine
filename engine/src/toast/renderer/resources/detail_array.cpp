#include "toast/renderer/resources/detail_array.hpp"

#include "ktx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <toast/assets/asset_manager.hpp>
#include <toast/voxel/assets/detail_texture.hpp>

namespace renderer {

namespace {

constexpr uint32_t k_min_size = 4;
constexpr uint32_t k_max_size = 1024;

constexpr std::array<uint8_t, 4> k_neutral {128, 128, 128, 255};

struct Decoded {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba;
};

auto decodeSource(uint64_t uid, std::string_view name) -> std::optional<Decoded> {
	ZoneScoped;
	if (uid == 0) {
		return std::nullopt;
	}

	const std::string uri = assets::AssetManager::getURI(toast::UID(uid));
	const std::optional<std::vector<uint8_t>> bytes = assets::AssetManager::get().tryLoadBytes(uri);
	if (!bytes || bytes->empty()) {
		TOAST_WARN("Render", "Detail texture '{}' source {} could not be read", name, uri);
		return std::nullopt;
	}

	ktxTexture2* ktx = nullptr;
	if (ktxTexture2_CreateFromMemory(bytes->data(), bytes->size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx) != KTX_SUCCESS ||
	    ktx == nullptr) {
		TOAST_WARN("Render", "Detail texture '{}' source {} is not readable KTX2", name, uri);
		return std::nullopt;
	}

	std::optional<Decoded> result;
	[&] {
		if (ktxTexture2_NeedsTranscoding(ktx) && ktxTexture2_TranscodeBasis(ktx, KTX_TTF_RGBA32, 0) != KTX_SUCCESS) {
			TOAST_WARN("Render", "Detail texture '{}' source {} failed to transcode", name, uri);
			return;
		}

		// VK_FORMAT_R8G8B8A8_UNORM and SRGB
		if (ktx->vkFormat != 37 && ktx->vkFormat != 43) {
			TOAST_WARN(
			    "Render",
			    "Detail texture '{}' source {} must import as uncompressed RGBA8 or Basis, vkFormat {} is not supported",
			    name,
			    uri,
			    ktx->vkFormat
			);
			return;
		}

		ktx_size_t offset = 0;
		if (ktxTexture2_GetImageOffset(ktx, 0, 0, 0, &offset) != KTX_SUCCESS) {
			return;
		}

		Decoded decoded;
		decoded.width = ktx->baseWidth;
		decoded.height = ktx->baseHeight;
		const size_t byte_count = static_cast<size_t>(decoded.width) * decoded.height * 4;
		if (decoded.width == 0 || decoded.height == 0 || offset + byte_count > ktx->dataSize) {
			return;
		}
		decoded.rgba.assign(ktx->pData + offset, ktx->pData + offset + byte_count);
		result = std::move(decoded);
	}();

	ktxTexture2_Destroy(ktx);
	return result;
}

auto roundUpPowerOfTwo(uint32_t value) -> uint32_t {
	uint32_t out = 1;
	while (out < value) {
		out <<= 1;
	}
	return out;
}

void resampleInto(const Decoded& source, uint32_t size, uint8_t* out) {
	ZoneScoped;
	const auto wrap = [](int64_t value, uint32_t extent) {
		const int64_t m = value % static_cast<int64_t>(extent);
		return static_cast<uint32_t>(m < 0 ? m + extent : m);
	};

	for (uint32_t y = 0; y < size; ++y) {
		float sy = ((static_cast<float>(y) + 0.5f) * static_cast<float>(source.height) / static_cast<float>(size)) - 0.5f;
		float fy = std::floor(sy);
		float ty = sy - fy;
		uint32_t y0 = wrap(static_cast<int64_t>(fy), source.height);
		uint32_t y1 = wrap(static_cast<int64_t>(fy) + 1, source.height);

		for (uint32_t x = 0; x < size; ++x) {
			float sx = ((static_cast<float>(x) + 0.5f) * static_cast<float>(source.width) / static_cast<float>(size)) - 0.5f;
			float fx = std::floor(sx);
			float tx = sx - fx;
			uint32_t x0 = wrap(static_cast<int64_t>(fx), source.width);
			uint32_t x1 = wrap(static_cast<int64_t>(fx) + 1, source.width);

			const auto texel = [&](uint32_t px, uint32_t py, uint32_t channel) {
				return static_cast<float>(source.rgba[(((static_cast<size_t>(py) * source.width) + px) * 4) + channel]);
			};

			for (uint32_t c = 0; c < 4; ++c) {
				float top = texel(x0, y0, c) + ((texel(x1, y0, c) - texel(x0, y0, c)) * tx);
				float bottom = texel(x0, y1, c) + ((texel(x1, y1, c) - texel(x0, y1, c)) * tx);
				float value = top + ((bottom - top) * ty);
				out[(((static_cast<size_t>(y) * size) + x) * 4) + c] = static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
			}
		}
	}
}

void downsample(const uint8_t* from, uint32_t from_size, uint8_t* to) {
	const uint32_t to_size = std::max(1u, from_size / 2);
	for (uint32_t y = 0; y < to_size; ++y) {
		for (uint32_t x = 0; x < to_size; ++x) {
			for (uint32_t c = 0; c < 4; ++c) {
				uint32_t sum = 0;
				for (uint32_t dy = 0; dy < 2; ++dy) {
					for (uint32_t dx = 0; dx < 2; ++dx) {
						uint32_t sx = std::min((x * 2) + dx, from_size - 1);
						uint32_t sy = std::min((y * 2) + dy, from_size - 1);
						sum += from[(((static_cast<size_t>(sy) * from_size) + sx) * 4) + c];
					}
				}
				to[(((static_cast<size_t>(y) * to_size) + x) * 4) + c] = static_cast<uint8_t>((sum + 2) / 4);
			}
		}
	}
}

}

auto buildDetailArray(const assets::DetailTexture& detail) -> std::optional<DetailArrayData> {
	ZoneScoped;
	const std::vector<assets::DetailTextureEntry> entries = detail.entries();
	if (entries.empty()) {
		return std::nullopt;
	}

	// R albedo
	// G roughness
	// B height
	struct Layer {
		uint8_t id = 0;
		std::array<std::optional<Decoded>, 3> channels;
	};

	std::vector<Layer> layers;
	uint32_t largest = 0;
	for (const assets::DetailTextureEntry& entry : entries) {
		Layer layer;
		layer.id = entry.id;
		const std::array<uint64_t, 3> sources {entry.albedo_uid, entry.roughness_uid, entry.height_uid};
		static constexpr std::array<std::string_view, 3> k_channel_names {"albedo", "roughness", "height"};
		for (size_t channel = 0; channel < 3; ++channel) {
			layer.channels[channel] = decodeSource(sources[channel], std::format("{} {}", entry.name, k_channel_names[channel]));
			if (layer.channels[channel]) {
				largest = std::max({largest, layer.channels[channel]->width, layer.channels[channel]->height});
			}
		}
		layers.push_back(std::move(layer));
	}

	DetailArrayData out;
	out.size = std::clamp(roundUpPowerOfTwo(std::max(largest, 1u)), k_min_size, k_max_size);
	out.layer_count = detail.layerCount();
	out.mip_levels = 1;
	while ((out.size >> out.mip_levels) > 0) {
		++out.mip_levels;
	}

	const uint64_t stride = out.layerStride();
	out.pixels.resize(stride * out.layer_count);

	for (uint64_t i = 0; i + 3 < out.pixels.size(); i += 4) {
		std::copy(k_neutral.begin(), k_neutral.end(), out.pixels.begin() + static_cast<std::ptrdiff_t>(i));
	}

	std::vector<uint8_t> resampled(static_cast<size_t>(out.size) * out.size * 4);
	for (const Layer& layer : layers) {
		if (layer.id >= out.layer_count) {
			continue;
		}

		uint8_t* base = out.pixels.data() + out.mipOffset(layer.id, 0);
		for (size_t channel = 0; channel < 3; ++channel) {
			if (!layer.channels[channel]) {
				continue;
			}
			resampleInto(*layer.channels[channel], out.size, resampled.data());
			for (size_t texel = 0; texel < static_cast<size_t>(out.size) * out.size; ++texel) {
				const uint32_t r = resampled[(texel * 4) + 0];
				const uint32_t g = resampled[(texel * 4) + 1];
				const uint32_t b = resampled[(texel * 4) + 2];
				base[(texel * 4) + channel] = static_cast<uint8_t>(((r * 299) + (g * 587) + (b * 114) + 500) / 1000);
			}
		}

		for (uint32_t mip = 1; mip < out.mip_levels; ++mip) {
			downsample(
			    out.pixels.data() + out.mipOffset(layer.id, mip - 1),
			    out.mipSize(mip - 1),
			    out.pixels.data() + out.mipOffset(layer.id, mip)
			);
		}
	}

	return out;
}

}
