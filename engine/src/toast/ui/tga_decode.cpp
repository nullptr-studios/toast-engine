#include "tga_decode.hpp"

#include <cstring>

namespace ui {

namespace {

constexpr size_t k_header_size = 18;

struct TgaHeader {
	uint8_t id_length;
	uint8_t color_map_type;
	uint8_t image_type;
	uint16_t width;
	uint16_t height;
	uint8_t bits_per_pixel;
	uint8_t descriptor;
};

auto readHeader(std::span<const uint8_t> d) -> TgaHeader {
	return {
	  .id_length = d[0],
	  .color_map_type = d[1],
	  .image_type = d[2],
	  .width = static_cast<uint16_t>(d[12] | (d[13] << 8)),
	  .height = static_cast<uint16_t>(d[14] | (d[15] << 8)),
	  .bits_per_pixel = d[16],
	  .descriptor = d[17],
	};
}

void writePixel(uint8_t* dst, const uint8_t* src, uint32_t bytes_per_pixel) {
	// TGA stores BGR(A)
	dst[0] = src[2];
	dst[1] = src[1];
	dst[2] = src[0];
	dst[3] = bytes_per_pixel == 4 ? src[3] : 0xFF;
}

}

auto decodeTga(std::span<const uint8_t> data) -> std::optional<TgaImage> {
	if (data.size() < k_header_size) {
		return std::nullopt;
	}

	const TgaHeader header = readHeader(data);
	const bool rle = header.image_type == 10;
	if ((header.image_type != 2 && !rle) || header.color_map_type != 0) {
		return std::nullopt;
	}
	if (header.bits_per_pixel != 24 && header.bits_per_pixel != 32) {
		return std::nullopt;
	}
	if (header.width == 0 || header.height == 0) {
		return std::nullopt;
	}

	const uint32_t bytes_per_pixel = header.bits_per_pixel / 8;
	const size_t pixel_count = static_cast<size_t>(header.width) * header.height;
	size_t cursor = k_header_size + header.id_length;

	TgaImage image {.width = header.width, .height = header.height, .pixels = std::vector<uint8_t>(pixel_count * 4)};

	if (rle) {
		size_t written = 0;
		while (written < pixel_count) {
			if (cursor >= data.size()) {
				return std::nullopt;
			}
			const uint8_t packet = data[cursor++];
			const uint32_t count = (packet & 0x7F) + 1;
			if (written + count > pixel_count) {
				return std::nullopt;
			}

			if ((packet & 0x80) != 0) {
				// one pixel repeated
				if (cursor + bytes_per_pixel > data.size()) {
					return std::nullopt;
				}
				for (uint32_t i = 0; i < count; i++) {
					writePixel(&image.pixels[(written + i) * 4], &data[cursor], bytes_per_pixel);
				}
				cursor += bytes_per_pixel;
			} else {
				// count literal pixels
				if (cursor + (static_cast<size_t>(count) * bytes_per_pixel) > data.size()) {
					return std::nullopt;
				}
				for (uint32_t i = 0; i < count; i++) {
					writePixel(
					    &image.pixels[(written + i) * 4], &data[cursor + (static_cast<size_t>(i) * bytes_per_pixel)], bytes_per_pixel
					);
				}
				cursor += static_cast<size_t>(count) * bytes_per_pixel;
			}
			written += count;
		}
	} else {
		if (cursor + (pixel_count * bytes_per_pixel) > data.size()) {
			return std::nullopt;
		}
		for (size_t i = 0; i < pixel_count; i++) {
			writePixel(&image.pixels[i * 4], &data[cursor + (i * bytes_per_pixel)], bytes_per_pixel);
		}
	}

	// bit 5 of the descriptor means top-left origin already
	// otherwise flip rows
	if ((header.descriptor & 0x20) == 0) {
		const size_t row_bytes = static_cast<size_t>(image.width) * 4;
		std::vector<uint8_t> row(row_bytes);
		for (uint32_t y = 0; y < image.height / 2; y++) {
			uint8_t* top = &image.pixels[y * row_bytes];
			uint8_t* bottom = &image.pixels[(image.height - 1 - y) * row_bytes];
			std::memcpy(row.data(), top, row_bytes);
			std::memcpy(top, bottom, row_bytes);
			std::memcpy(bottom, row.data(), row_bytes);
		}
	}

	return image;
}

}
