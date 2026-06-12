/**
 * @file gltf_importer.hpp
 * @author Xein
 * @date 12 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once
#include <filesystem>

namespace assets {
auto importGltf(const std::filesystem::path& path);
}
