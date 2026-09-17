/**
 * @file crash_handler.hpp
 * @author dario
 * @date 14/09/2026
 *
 * @brief The reporter runs out of process since after a crash the heap, loader and locks here may be unusable
 */

#pragma once
#include <filesystem>
#include <toast/export.hpp>

namespace toast::crash {

/// @brief No-op off Windows when disabled or when the reporter executable is missing
TOAST_API void install() noexcept;

TOAST_API void setDumpDirectory(const std::filesystem::path& directory) noexcept;

TOAST_API void triggerTestCrash(int kind) noexcept;

}
