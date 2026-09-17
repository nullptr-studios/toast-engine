/**
 * @file voxel_material_library.hpp
 * @author dario
 * @date 11/09/2026
 */

#pragma once
#include "core_types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toast/voxel/palette.hpp>
#include <vector>

namespace assets {

struct VoxelMaterialInfo {
	std::string name;

	uint64_t impact_sound = 0;

	std::array<uint8_t, 3> dust_colour {128, 128, 128};

	std::string tag;

	[[nodiscard]]
	auto operator==(const VoxelMaterialInfo&) const -> bool = default;
};

class TOAST_API VoxelMaterialLibrary : public Asset, public ISaveable {
public:
	VoxelMaterialLibrary(toast::voxel::MaterialLibrary library, std::vector<VoxelMaterialInfo> info);

	/// @brief Throws on unrepresentable values and warns on wrong but representable ones
	[[nodiscard]]
	static auto fromToml(const toml::table& table) -> std::unique_ptr<VoxelMaterialLibrary>;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "voxel_material_library";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	[[nodiscard]]
	auto library() const noexcept -> const toast::voxel::MaterialLibrary& {
		return m_library;
	}

	[[nodiscard]]
	auto info(uint32_t index) const -> const VoxelMaterialInfo& {
		return m_info.at(index);
	}

	[[nodiscard]]
	auto size() const noexcept -> uint32_t {
		return static_cast<uint32_t>(m_library.materials.size());
	}

	[[nodiscard]]
	auto indexOf(std::string_view name) const -> std::optional<uint32_t>;

private:
	toast::voxel::MaterialLibrary m_library;

	/// Parallel to m_library.materials
	std::vector<VoxelMaterialInfo> m_info;
};

}
