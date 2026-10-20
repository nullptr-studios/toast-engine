/**
 * @file renderer_settings.hpp
 * @author dario
 * @date 16/08/2026
 */

#pragma once
#include <string_view>
#include <toast/export.hpp>

namespace renderer {

class VulkanRenderer;

/// F3 in game view flips it
inline constexpr std::string_view k_debug_overlay_in_game = "renderer.debug.overlay_in_game";

/// @note Call after Settings::load() and before the passes exist
TOAST_API void registerRendererStartupSettings();

/// @note Call after the post process passes exist
TOAST_API void registerRendererSettings(VulkanRenderer& renderer);

}
