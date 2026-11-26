/**
 * @file cook.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Turns a project's config files into the BSON a
 */

#pragma once
#include <filesystem>
#include <toast/export.hpp>

namespace toast::params {

TOAST_API auto cookConfigs(
    const std::filesystem::path& config_dir, const std::filesystem::path& toast_file, const std::filesystem::path& stage_dir
) -> bool;

}
