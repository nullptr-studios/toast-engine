/**
 * @file voxel_palette.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "core_types.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <toast/voxel/palette.hpp>
#include <vector>

namespace assets {

class TOAST_API VoxelPalette : public Asset, public ISaveable {
public:
	VoxelPalette(voxel::Palette palette, uint64_t library_uid, std::vector<uint8_t> defaulted);

	/// @brief Throws on values an entry cannot hold and leaves material existence to validatePalette
	[[nodiscard]]
	static auto fromToml(const toml::table& table) -> std::unique_ptr<VoxelPalette>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "voxel_palette";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto palette() const noexcept -> const voxel::Palette& {
		return m_palette;
	}

	[[nodiscard]]
	auto libraryUid() const noexcept -> uint64_t {
		return m_library_uid;
	}

	/// @brief Round tripped so the warning survives a save
	[[nodiscard]]
	auto defaultedEntries() const noexcept -> const std::vector<uint8_t>& {
		return m_defaulted;
	}

private:
	voxel::Palette m_palette;
	uint64_t m_library_uid = 0;
	std::vector<uint8_t> m_defaulted;
};

}
