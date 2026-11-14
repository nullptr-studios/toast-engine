/**
 * @file detail_array.hpp
 * @author Xein
 * @date 14 Nov 2026
 */

#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace assets {
class DetailTexture;
}

namespace renderer {

struct DetailArrayData {
	uint32_t size = 0;
	uint32_t mip_levels = 0;
	uint32_t layer_count = 0;

	std::vector<uint8_t> pixels;

	[[nodiscard]]
	auto mipSize(uint32_t mip) const noexcept -> uint32_t {
		const uint32_t extent = size >> mip;
		return extent > 0 ? extent : 1;
	}

	[[nodiscard]]
	auto layerStride() const noexcept -> uint64_t {
		uint64_t total = 0;
		for (uint32_t mip = 0; mip < mip_levels; ++mip) {
			total += static_cast<uint64_t>(mipSize(mip)) * mipSize(mip) * 4;
		}
		return total;
	}

	[[nodiscard]]
	auto mipOffset(uint32_t layer, uint32_t mip) const noexcept -> uint64_t {
		uint64_t offset = static_cast<uint64_t>(layer) * layerStride();
		for (uint32_t i = 0; i < mip; ++i) {
			offset += static_cast<uint64_t>(mipSize(i)) * mipSize(i) * 4;
		}
		return offset;
	}
};

[[nodiscard]]
auto buildDetailArray(const assets::DetailTexture& detail) -> std::optional<DetailArrayData>;

}
