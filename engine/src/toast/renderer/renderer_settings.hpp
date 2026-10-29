/**
 * @file renderer_settings.hpp
 * @author dario
 * @date 16/08/2026
 */

#pragma once
#include <optional>
#include <string_view>
#include <toast/export.hpp>
#include <toast/settings/settings.hpp>

namespace renderer {

class VulkanRenderer;

inline constexpr std::string_view k_hdr_output = "renderer.display.hdr";

/// F3 in game view flips it
inline constexpr std::string_view k_debug_overlay_in_game = "renderer.debug.overlay_in_game";

/// @note Call after Settings::load() and before the passes exist
TOAST_API void registerRendererStartupSettings();

/// @note Call after the post process passes exist
TOAST_API void registerRendererSettings(VulkanRenderer& renderer);

/// Nothing under Custom or for a setting no tier drives
[[nodiscard]]
auto presetDrivenValue(std::string_view key) -> std::optional<toast::settings::Value>;

}
