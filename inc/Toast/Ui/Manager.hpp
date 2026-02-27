/**
 * @file Manager.hpp
 * @author Dante Harper
 * @date 26/02/26
 *
 * @brief general ui instance
 */

#pragma once

#include "Toast/Event/ListenerComponent.hpp"

#include <Toast/Renderer/HUD/HUDLayer.hpp>
#include <Toast/Window/Window.hpp>

namespace ui {
class UiSystem {
	struct {
		renderer::HUD::HUDLayer* layer;
		event::ListenerComponent listener;
	} m;

	void Configure();

public:
	UiSystem(toast::Window& window, bool msaa);
};
}
