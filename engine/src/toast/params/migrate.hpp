/**
 * @file migrate.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief One-time moves from the old settings layout to config
 */

#pragma once
#include <filesystem>
#include <toast/export.hpp>

namespace toast::params {

TOAST_API auto migrateProjectToast(const std::filesystem::path& toast_file, const std::filesystem::path& config_dir) -> bool;

TOAST_API auto upgradeLegacyConfig(const std::filesystem::path& config_dir) -> bool;

TOAST_API auto migrateLegacyUserSettings(const std::filesystem::path& old_file, const std::filesystem::path& saved_config_dir)
    -> bool;

}
