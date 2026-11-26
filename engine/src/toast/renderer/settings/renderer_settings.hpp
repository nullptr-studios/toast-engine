/**
 * @file renderer_settings.hpp
 * @author dario
 * @date 16/08/2026
 */

#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/value.hpp>

namespace renderer {

class VulkanRenderer;

inline constexpr std::string_view k_hdr_output = "renderer.display.hdr";

/// F3 in game view flips it
inline constexpr std::string_view k_debug_overlay_in_game = "renderer.display.debug_overlay";

TOAST_API void registerRendererStartupSettings();

/// @note Call after the post process passes exist
TOAST_API void registerRendererSettings(VulkanRenderer& renderer);

[[nodiscard]]
TOAST_API auto presetDrivenValue(std::string_view path) -> std::optional<toast::params::Value>;

TOAST_API void testSetPreset(uint32_t preset);

}
