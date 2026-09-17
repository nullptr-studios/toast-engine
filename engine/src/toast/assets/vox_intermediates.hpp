/**
 * @file vox_intermediates.hpp
 * @author dario
 * @date 12/09/2026
 */

#pragma once
#include "vox_import.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <toast/export.hpp>
#include <vector>

namespace assets {

struct VoxIntermediateModel {
	std::string file_name;

	uint32_t model = 0;

	uint32_t solid_voxels = 0;
};

struct VoxIntermediates {
	std::string base_name;

	std::vector<VoxIntermediateModel> models;

	std::string palette_file_name;

	std::string manifest_file_name;

	std::vector<std::string> warnings;
};

/// @throws std::runtime_error when the source cannot be read or parsed
[[nodiscard]]
TOAST_API auto
    writeVoxIntermediates(const std::filesystem::path& source, const std::filesystem::path& out_dir, uint64_t palette_uid)
        -> VoxIntermediates;

/// @throws std::runtime_error when the manifest is invalid
TOAST_API void voxManifestToPrefab(const std::filesystem::path& manifest_path, const std::filesystem::path& out_path);

}
