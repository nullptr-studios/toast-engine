/**
 * @file renderer_settings.hpp
 * @author dario
 * @date 16/08/2026
 */

#pragma once
#include <toast/export.hpp>

namespace renderer {

class VulkanRenderer;

/// @note Call after Settings::load() and before the passes exist
TOAST_API void registerRendererStartupSettings();

/// @note Call after the post process passes exist
TOAST_API void registerRendererSettings(VulkanRenderer& renderer);

}
