/// @file game_view_texture.hpp
/// @author dario
/// @date 23/11/2026

#pragma once

#include "toast/renderer/core/vulkan_common.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace renderer {

/// In eShaderReadOnlyOptimal once the view pass ran
struct GameViewTexture {
	vk::ImageView view;
	vk::Extent2D extent;
	/// Keeps the image alive
	std::shared_ptr<const void> keepalive;
};

/// Any thread
auto findGameViewTexture(std::string_view name) -> std::optional<GameViewTexture>;

}
